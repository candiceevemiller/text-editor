/* Name: Cedit
 * Purpose: Simple text editor Cedit - Candice's Editor
 * Author: Candice Miller
 */

/*** includes***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CEDIT_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

/*** data ***/

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx, cy;
    int rowoff; // row offset for scroll
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char* s);
void enableRawMode();
void disableRawMode();
int editorReadKey();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

/*** row operations ***/
void editorAppendRow(char *s, size_t len);

/*** file i/o ***/

void editorOpen(char* filename);

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/*** input ***/
void editorMoveCursor(int key); 
void editorProcessKeypress();

/*** output ***/
void moveCursorToStart();
void clearScreen();
void editorDrawRows(struct abuf *ab);
void editorScroll();
void editorRefreshScreen();

/*** init ***/
void initEditor();

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

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

int editorReadKey()
{
    // Contains logic to read key presses
    int nread;
    char c = '\0';
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        // Home and End have multiple possible esc sequences
                        case '1':
                        case '7':
                            return HOME_KEY;
                        case '4':
                        case '8':
                            return END_KEY;    
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            }
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';    
    } else {
        return c;
    }
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

/*** row operations ***/

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

/*** file i/o ***/

void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                            line[linelen - 1] == '\r'))
        linelen --;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
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
void editorMoveCursor(int key)
{
    // Handle cursor movement

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screencols - 1) {
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c) {
        // exit program
        case CTRL_KEY('q'):
            clearScreen();
            exit(0);
            break;

        // Page Up/Page Down
        case PAGE_UP:
        case PAGE_DOWN:
        {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

        // Home and End
        case HOME_KEY:
            E.cx=0;
            break;

        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        // Move Cursor
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
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
    int r; // rows
    for (r = 0; r < E.screenrows; r++) {
        int filerow = r + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && r == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), 
                "Cedit -- version %s", CEDIT_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;

                // padding is half the remaining cols after subtracting welcome message
                int padding = (E.screencols - welcomelen) / 2;
                // write a tilde to the first col of welcome line
                if (padding) {
                    // ~ at the beginning of line like vim
                    abAppend(ab, "~", 1);
                    padding--;
                }
                // exhaust left padding and append welcome message
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                // ~ at the beginning of line like vim
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].size;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, E.row[filerow].chars, len);
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

void editorScroll()
{
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;
    // don't use clear screen func here because 
    // write is inneficient on draw

    //hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    //return cursor to start
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // Move cursor to E.cy+1, E.cx+1
    // (Terminal uses 1 indexed vals)
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
    abAppend(&ab, buf, strlen(buf));

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
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.numrows = 0;
    E.row = NULL;
    // Initializes vals in E struct

    // set row/col size && die on fail
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}