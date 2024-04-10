/* Name: cedit.c
 * Purpose: Simple text editor Cedit - Candice's Editor
 * Author: Candice Miller
 */

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*
 * BEGIN BLOCK: DECLARATIONS
 */

// raw mode hides input in terminal
// enable/disable flips behavior
struct termios orig_termios; //original terminal config, used by both enable/disable
void enableRawMode();
void disableRawMode();

/*
 * BEGIN BLOCK: MAIN
 */
int main(void)
{
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c!='q');
    return 0;
}

/*
 * BEGIN BLOCK: ENABLE/DISABLE RAW MODE
 */

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}