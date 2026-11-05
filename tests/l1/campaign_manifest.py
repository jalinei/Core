#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file campaign_manifest.py
## @brief Stamp L1 integration scope and model identity into the existing campaign.
"""Record metadata without converting blocked formal cases to passing criteria."""
from pathlib import Path
import hashlib,json,sys,importlib.metadata,subprocess
import pulsim
HERE=Path(__file__).resolve().parent
out=Path(sys.argv[1]);params=json.loads((HERE/'plant/params.yaml').read_text());contract=json.loads((HERE/'contract.json').read_text())
meta={'plant_model':'switched full_model.pulsim@'+params['model_sha256'][:12]+' (switching integration + selected application criteria)',
      'model_sha256':params['model_sha256'],'contract':contract['version'],
      'profile':contract['application_profile'],'integration_only':False,'pulsim':pulsim.__version__,
      'packages':{name:importlib.metadata.version(name) for name in ['pulsim','pulsimgui','numpy','PySide6','scipy']},
      'integration_sources':{str(p.relative_to(HERE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in HERE.rglob('*') if p.is_file() and p.suffix in {'.py','.sh','.cpp','.h','.json'} and 'build' not in p.parts and 'derived' not in p.parts},
      'capabilities':params['capabilities'],'pv_variant_available':True,
      'pv_profile':json.loads((HERE/'plant/pv-profile.json').read_text()),
      'contract_sha256':hashlib.sha256((HERE/'contract.json').read_bytes()).hexdigest()}
(out/'l1-manifest.json').write_text(json.dumps(meta,indent=2)+'\n')

(out/'pip-freeze.txt').write_text(subprocess.check_output([sys.executable,'-m','pip','freeze'],text=True))
