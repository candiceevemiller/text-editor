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
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

/*** defines ***/
#define CEDIT_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char* s);
void enableRawMode();
void disableRawMode();
char editorReadKey();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/*** input ***/
void editorProcessKeypress();

/*** output ***/
void moveCursorToStart();
void clearScreen();
void editorDrawRows(struct abuf *ab);
void editorRefreshScreen();

/*** init ***/
void initEditor();

int main(void)
{
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}

/*** terminal ***/
void enableRawMode()
{
    // Get original terminal config
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    // Automatically run disableRawMode at program exit
    atexit(disableRawMode);
    
    // Copy orig config into raw
    struct termios raw = E.orig_termios;

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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void die(const char* s)
{
    // Basic Error Logging and exit

    //cls first so error message not overwritten
    clearScreen();

    perror(s); //print error
    exit(1); //return 1 (error) on program exit
}

char editorReadKey()
{
    // Contains logic to read key presses
    int nread;
    char c = '\0';
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    return c;
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    // prints cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // reads cursor position into buffer
    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    // terminates buffer string
    buf[i] = '\0';

    // confirms buffer is escape sequence
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // reads buffer into pointers for rows and cols
    // no & because our function params are pointers
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    // Terminal Input/Output Control Get Window Size
    // TIOCGWINSZ
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // moves cursor down and right 999 times
        // C and B prevent leaving edge of screen
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows,cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len+len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** input ***/
void editorProcessKeypress()
{
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            clearScreen();
            exit(0);
            break;
    }
}

void moveCursorToStart()
{
    // [H moves cursor to top of terminal
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** output ***/
void clearScreen()
{
    // [2J clears whole screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    moveCursorToStart();
}

void editorDrawRows(struct abuf *ab)
{
    // draw ~ like vim

    int r; // rows
    for (r = 0; r < E.screenrows; r++) {
        if (r == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), 
            "Cedit -- version %s", CEDIT_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        // clear right of line
        // more efficient than screen redraw
        abAppend(ab, "\x1b[K", 3);

        // don't draw on last line
        if (r < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;
    // don't use clear screen func here because 
    // write is inneficient on draw

    //hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    //return cursor to start
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    //return cursor to start
    abAppend(&ab, "\x1b[H", 3);
    //show cursor again
    abAppend(&ab, "\x1b[?25h", 6);

    // write buffer
    write(STDOUT_FILENO, ab.b, ab.len);

    // free buffer
    abFree(&ab);
}

/*** init ***/
void initEditor()
{
    // Initializes vals in E struct

    // set row/col size && die on fail
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}