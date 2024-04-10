/* Name: Cedit
 * Purpose: Simple text editor Cedit - Candice's Editor
 * Author: Candice Miller
 */

/*
 * BEGIN Block: Includes
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*
 * BEGIN BLOCK: DECLARATIONS
 */

/*** Data ***/
struct termios orig_termios; //original terminal config, used by both enable/disable

/*** terminal ***/
void die(const char* s);
void enableRawMode();
void disableRawMode();


/*
 * BEGIN BLOCK: MAIN
 */
int main(void)
{
    enableRawMode();

    while (1) {
        char c = '\0';

        // on some systems timeout returns error of EAGAIN but it's supposed to return 0
        // ignore that case in error logging read
        if ((read(STDIN_FILENO, &c, 1) == -1) && errno != EAGAIN) die("read");
        
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }

        if (c == 'q') break;
    }

    return 0;
}

/*
 * BEGIN BLOCK: ENABLE/DISABLE RAW MODE
 */

void enableRawMode()
{
    // Get original terminal config
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");

    // Automatically run disableRawMode at program exit
    atexit(disableRawMode);
    
    // Copy orig config into raw
    struct termios raw = orig_termios;

    /* Set the terminal config flags to what we want using bitwise arithmetic
     * & (and) and ~ (not) force these flags to be false
     * All code comes from termios.h
     *
     * 
     * INPUT FLAGS - c_iflags:
     * -------------------------------
     * IXON - handles ctrl-s ctrl-q (suspend/resume)
     * ICRNL - CR carriage return NL new line. Replaces \r with \n on ctrl-m
     * Likely useless on modern terminals but part of turning on raw mode:
     * BRKINT - sends interrupt (ctrl-c SIGINT) on a break condition
     * INPCK - checks parity
     * ISTRIP - strips 8th bit of each input byte
     * 
     * OUTPUT FLAGS - c_oflags:
     * -------------------------------
     * OPOST - output post processing converts \n to \r\n related to ICRNL
     *         practically this means we're using \r\n manually in this file
     * 
     * CONTROL FLAGS - c_cflags:
     * -------------------------------
     * CS8 - Not a flag, a mask with multiple bits - sets char size to 8 bits
     *
     * LOCAL FLAGS - c_lflags:
     * -------------------------------
     * ECHO - echos what you write to the terminal
     * ICANON - reads by line instead of by byte
     * ISIG - controls ctrl-c ctrl-z (interrupt / and suspend to background)
     * IEXTEN - handles ctrl-v
     * 
     * We want byte-wise reading, no echo, override escape chars
     */
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    raw.c_cc[VMIN] = 0; // Sets minimum bytes to 0 - reads input after there's any input to be read
    raw.c_cc[VTIME] = 1; // Times out input read after 1/10th second

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void disableRawMode()
{
    // Try to set terminal back to orignial config
    // && Flush any trailing characters (TCSAFLUSH)
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void die(const char* s)
{
    // Basic Error Logging
    
    perror(s); //print error
    exit(1); //return 1 (error) on program exit
}
