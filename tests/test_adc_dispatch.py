#!/usr/bin/env python3
"""Exercise the real ADC dispatcher with a deterministic DMA producer.

Checks task and interrupt modes, partial scans, ring wraps, buffer swaps,
peek retention and saturation on every ADC under ASan/UBSan. An optional
--reference source additionally compares all non-saturating sample streams.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <array>
#include "dispatch.cpp"
static unsigned channels;
static unsigned target;
static unsigned acquired;
uint32_t adc_get_enabled_channels_count(uint8_t n) { return n==target?channels:0; }
void dma_configure_adc_acquisition(uint8_t,bool,uint16_t*,size_t) {}
uint32_t dma_get_retrieved_data_count(uint8_t) { return acquired; }
int main(int argc,char** argv) {
 assert(argc==5);
 target=std::atoi(argv[4]);
 channels=std::atoi(argv[1]);bool interrupts=std::atoi(argv[2]);bool overflow=std::atoi(argv[3]);
 data_dispatch_init(interrupts?interrupt:task,23);
 std::vector<std::vector<uint16_t>> expected(channels);
 unsigned produced=0;unsigned cursor=0;unsigned half=0;
 for (unsigned turn=0;turn<200;++turn) {
  acquired=interrupts?channels:(turn*17)%(dma_buffer_sizes[target-1]+1);
  if(interrupts)cursor=half*channels;
  for(unsigned i=0;i<acquired;++i){
   unsigned channel=cursor%channels;
   uint16_t value=uint16_t(++produced);
   dma_main_buffers[target-1][cursor]=value;
   if(expected[channel].size()<CHANNELS_BUFFERS_SIZE)expected[channel].push_back(value);
   cursor=(cursor+1)%dma_buffer_sizes[target-1];
  }
  if(interrupts)half^=1;
  data_dispatch_do_full_dispatch();
  if(!overflow || turn%47==46 || turn==199) {
   for(unsigned ch=0;ch<channels;++ch){
    unsigned count;
    auto* data=data_dispatch_get_acquired_values(target,ch+1,count);
    assert(count==expected[ch].size());
    for(unsigned i=0;i<count;++i){assert(data[i]==expected[ch][i]);std::printf("%u,",data[i]);}
    auto latest=count?data[count-1]:peek_memory[target-1][ch];
    assert(data_dispatch_peek_acquired_value(target,ch+1)==latest);
    unsigned emptyCount=999;assert(data_dispatch_get_acquired_values(target,ch+1,emptyCount)==nullptr);assert(emptyCount==0);
    expected[ch].clear();
   }
  }
 }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='adc-dispatch-') as directory:
        tmp = Path(directory)
        (tmp / 'zephyr').mkdir()
        (tmp / 'zephyr/kernel.h').write_text('#include <cstdlib>\n#define k_malloc std::malloc\n#define k_calloc std::calloc\n')
        (tmp / 'adc.h').write_text('#include <cstdint>\nconstexpr unsigned ADC_COUNT=5;\nuint32_t adc_get_enabled_channels_count(uint8_t);\n')
        (tmp / 'SpinAPI.h').write_text('#define __STATIC_INLINE static inline\n')
        (tmp / 'soc.h').write_text('')
        src = ROOT / 'zephyr/modules/owntech_spin_api/zephyr/src/data'
        for name in ['dma.h', 'data_dispatch.h']:
            (tmp / name).write_text((src / name).read_text())
        (tmp / 'adc_dispatch_profile.h').write_text((ROOT / 'zephyr/modules/owntech_spin_api/zephyr/public_api/adc_dispatch_profile.h').read_text())
        (tmp / 'harness.cpp').write_text(HARNESS)
        def compile_source(source, label):
            (tmp / 'dispatch.cpp').write_text(source.read_text())
            subprocess.run(['g++', '-std=c++20', '-O2', '-fsanitize=address,undefined',
                            '-I' + str(tmp), str(tmp / 'harness.cpp'), '-o', str(tmp / label)], check=True)
        compile_source(src / 'data_dispatch.cpp', 'optimized')
        if args.reference:
            compile_source(args.reference, 'reference')
        cases = 0
        for adc in range(1, 6):
            for channels in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16]:
                for interrupt_mode in [0, 1]:
                    for saturation in [0, 1]:
                        inputs = list(map(str, [channels, interrupt_mode, saturation, adc]))
                        result = subprocess.run([str(tmp / 'optimized')] + inputs,
                                                env=env, check=True, capture_output=True)
                        if args.reference and not saturation:
                            reference = subprocess.run([str(tmp / 'reference')] + inputs,
                                                       env=env, check=True, capture_output=True)
                            assert result.stdout == reference.stdout, inputs
                        cases += 1
        print(f'{cases} cases passed under ASan/UBSan (200 dispatches per case).')


if __name__ == '__main__':
    main()
