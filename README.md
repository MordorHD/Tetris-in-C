# Tetris-in-C

Just another Tetris version I made for fun.
The drop sound effect of the tetronomino is me slamming my desk, I didn't find the real sound effect online, so I had to improvise. I also did the font and textures myself.

When the program is closed, it creates a `Tetris.save` (if one doesn't already exist) file which will keep track of all highscores and typed names.

Only on windows.

## Compiling
Compile using linker options: `-lgdi32 -lwinmm` <br>
Navigate to the where you have extracted the zip file to with your console window using `cd` <br>
Be sure to have `gcc` command available, just type `gcc` to confirm. <br>
When this is not the case, you have to use `set path=%path%;<file path of gcc.exe>` <br>
`gcc main.c -lgdi32 -lwinmm & a` will compile and execute the program.

## Running the program
So, when you you used above steps to compile, the program should already run. <br>
When you don't have `gcc` to compile, download the project as zip, extract all files into one folder and run `Tetris.exe`


## Controls
### Menu
<kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> or arrow keys to move
### In-Game
<kbd>A</kbd>, <kbd>LEFT</kbd> - Move left <br>
<kbd>D</kbd>, <kbd>RIGHT</kbd> - Move right <br>
<kbd>W</kbd>, <kbd>UP</kbd>, <kbd>I</kbd> - Rotate left <br>
<kbd>J</kbd> - Rotate right <br>
<kbd>S</kbd> - Fast fall
<kbd>M</kbd> - Mute music <br>
<kbd>N</kbd> - Mute all music and sounds <br>
<kbd>C</kbd> - Disable colors <br>
<kbd>RETURN</kbd>, <kbd>ESCAPE</kbd>, <kbd>P</kbd> - Pause

