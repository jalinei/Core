
## Code format 
Code format should follow https://kernel.org/doc/html/latest/process/coding-style.html

With the following exceptions: 

- The line length is 100 columns or fewer. In the documentation, longer lines for URL references are an allowed exception.

- Add braces to every `if`, `else`, `do`, `while`, `for` and `switch` body, even for single-line code blocks. Use the `--ignore BRACES` flag to make checkpatch stop complaining.

- Use spaces instead of tabs to align comments after declarations, as needed.

- Use C89-style single line comments, `/*  */`. The C99-style single line comment, `//`, is not allowed.

- Use `/**  */` for doxygen comments that need to appear in the documentation.

- Avoid using binary literals (constants starting with `0b`).

- Avoid using non-ASCII symbols in code, unless it significantly improves clarity, avoid emojis in any case.


## Checking code conformity 

``` checkpatch does not currently run on Windows. ```

Code conformity can be checked automatically using `checkpatch`. 
Checkpatch is used during CI to check code style after a PR.

To check code conformity :
- Mark the script as an executable
``` chmod +x ./checkpatch.pl```
- Launch the check using 
``` ./checkpatch.pl -f --no-tree file_to_check.c```

## Code formatting 

A tool called Clang-format can be used to format the code. 

- install clang-format using `sudo apt install clang-format`
- install vscode extension `clang-format` from `Xaver Hellauer`

clang-format automatically load coding rules contained in .clang-format at the project root. 


