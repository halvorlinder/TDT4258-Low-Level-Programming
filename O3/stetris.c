#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <linux/input.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <poll.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <dirent.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

// The game state can be used to detect what happns on the playfield
#define GAMEOVER 0
#define ACTIVE (1 << 0)
#define ROW_CLEAR (1 << 1)
#define TILE_ADDED (1 << 2)

#define NUMBER_OF_COLORS 8
#define FB_DATA_SIZE 8 * 8 * 2

#define SCREEN 0
#define JOYSTICK 1

// If you extend this structure, either avoid pointers or adjust
// the game logic allocate/deallocate and reset the memory
typedef struct
{
  bool occupied;
  u_int32_t color_index;
} tile;

typedef struct
{
  unsigned int x;
  unsigned int y;
} coord;

typedef struct
{
  coord const grid;                     // playfield bounds
  unsigned long const uSecTickTime;     // tick rate
  unsigned long const rowsPerLevel;     // speed up after clearing rows
  unsigned long const initNextGameTick; // initial value of nextGameTick

  unsigned int tiles; // number of tiles played
  unsigned int rows;  // number of rows cleared
  unsigned int score; // game score
  unsigned int level; // game level

  tile *rawPlayfield; // pointer to raw memory of the playfield
  tile **playfield;   // This is the play field array
  unsigned int state;
  coord activeTile; // current tile

  unsigned long tick;         // incremeted at tickrate, wraps at nextGameTick
                              // when reached 0, next game state calculated
  unsigned long nextGameTick; // sets when tick is wrapping back to zero
                              // lowers with increasing level, never reaches 0
} gameConfig;

gameConfig game = {
    .grid = {8, 8},
    .uSecTickTime = 10000,
    .rowsPerLevel = 2,
    .initNextGameTick = 50,
};

u_int16_t colors[] = {24588, 63488, 1247, 64710, 2016, 65504, 64735, 38943};

u_int32_t tile_index;

u_int16_t *fb_data;
int joyfd;
int fbfd;

// had to move this as i needed to use it further up
static inline bool tileOccupied(coord const target)
{
  return game.playfield[target.y][target.x].occupied;
}

// this function finds the correct device and returns its fd
int getfd(char *name, char *dir, int flags, int type)
{
  // get stream for iterating over files in the directory
  struct dirent *dp;
  DIR *dfd;

  if ((dfd = opendir(dir)) == NULL)
  {
    return -1;
  }

  char devname[100];
  struct fb_fix_screeninfo info;
  char filename[100];

  // iterate over files and compare to name to find the correct device
  while ((dp = readdir(dfd)) != NULL)
  {
    // get the absolute name of the file and open
    sprintf(filename, "%s/%s", dir, dp->d_name);
    int fd = open(filename, flags);
    // extract device name for the right case
    if (type == SCREEN)
    {
      ioctl(fd, FBIOGET_FSCREENINFO, &info);
      strcpy(devname, info.id);
    }
    else if (type == JOYSTICK)
    {
      ioctl(fd, EVIOCGNAME(sizeof(devname)), devname);
    }
    // compare names
    if (strcmp(name, devname) == 0)
    {
      return fd;
    }
    close(fd);
  }
  // not found
  return -1;
}

// This function is called on the start of your application
// Here you can initialize what ever you need for your task
// return false if something fails, else true
bool initializeSenseHat()
{
  // get a fd for the screen
  fbfd = getfd("RPi-Sense FB", "/dev", O_RDWR, SCREEN);
  // get a fd for the joystick
  joyfd = getfd("Raspberry Pi Sense HAT Joystick", "/dev/input", O_RDONLY | O_NONBLOCK, JOYSTICK);

  // success
  if (fbfd >= 0)
  {
    // size of the memory occupied by the screen (w*h*bytes)
    // memory map the screen and get a pointer to its memory
    fb_data = mmap(0, FB_DATA_SIZE, PROT_WRITE, MAP_SHARED, fbfd, 0);
    // clear the screen
    memset(fb_data, 0, FB_DATA_SIZE);
    return true;
  }
  return false;
}

// This function is called when the application exits
// Here you can free up everything that you might have opened/allocated
void freeSenseHat()
{
  // clear screen
  memset(fb_data, 0, FB_DATA_SIZE);
  // remove memory mapping
  munmap(fb_data, FB_DATA_SIZE);
  // close device files
  close(fbfd);
  close(joyfd);
  return;
}

// This function should return the key that corresponds to the joystick press
// KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, with the respective direction
// and KEY_ENTER, when the the joystick is pressed
// !!! when nothing was pressed you MUST return 0 !!!
int readSenseHatJoystick()
{
  // do polling similar to keyboard polling
  struct pollfd pollJoy = {
      .fd = joyfd,
      .events = POLLIN};
  struct input_event input;
  int lkey = 0;

  // poll without timeout
  if (poll(&pollJoy, 1, 0))
  {
    // read the input event into the struct
    read(pollJoy.fd, &input, sizeof(input));
    // check that the joystick active
    if (input.type == EV_KEY && input.value == 1)
    {
      lkey = input.code;
    }
  }
  switch (lkey)
  {
  case 28:
    return KEY_ENTER;
  case 103:
    return KEY_UP;
  case 108:
    return KEY_DOWN;
  case 106:
    return KEY_RIGHT;
  case 105:
    return KEY_LEFT;
  }
  return 0;
}

// This function should render the gamefield on the LED matrix. It is called
// every game tick. The parameter playfieldChanged signals whether the game logic
// has changed the playfield
void renderSenseHatMatrix(bool const playfieldChanged)
{
  // if nothing changed, just return
  if (!playfieldChanged)
    return;
  // iterate over the grid
  for (unsigned int y = 0; y < game.grid.y; y++)
  {
    for (unsigned int x = 0; x < game.grid.x; x++)
    {
      coord const check_tile = {x, y};
      // either draw a color or turn off, based on the tile being occupied
      if (tileOccupied(check_tile))
      {
        fb_data[8 * y + x] = colors[game.playfield[y][x].color_index % NUMBER_OF_COLORS];
      }
      else
      {
        fb_data[8 * y + x] = 0;
      }
    }
  }
}

// The game logic uses only the following functions to interact with the playfield.
// if you choose to change the playfield or the tile structure, you might need to
// adjust this game logic <> playfield interface

static inline void newTile(coord const target)
{
  game.playfield[target.y][target.x].occupied = true;
  game.playfield[target.y][target.x].color_index = tile_index;
  tile_index++;
}

static inline void copyTile(coord const to, coord const from)
{
  memcpy((void *)&game.playfield[to.y][to.x], (void *)&game.playfield[from.y][from.x], sizeof(tile));
}

static inline void copyRow(unsigned int const to, unsigned int const from)
{
  memcpy((void *)&game.playfield[to][0], (void *)&game.playfield[from][0], sizeof(tile) * game.grid.x);
}

static inline void resetTile(coord const target)
{
  memset((void *)&game.playfield[target.y][target.x], 0, sizeof(tile));
}

static inline void resetRow(unsigned int const target)
{
  memset((void *)&game.playfield[target][0], 0, sizeof(tile) * game.grid.x);
}

static inline bool rowOccupied(unsigned int const target)
{
  for (unsigned int x = 0; x < game.grid.x; x++)
  {
    coord const checkTile = {x, target};
    if (!tileOccupied(checkTile))
    {
      return false;
    }
  }
  return true;
}

static inline void resetPlayfield()
{
  for (unsigned int y = 0; y < game.grid.y; y++)
  {
    resetRow(y);
  }
}

// Below here comes the game logic. Keep in mind: You are not allowed to change how the game works!
// that means no changes are necessary below this line! And if you choose to change something
// keep it compatible with what was provided to you!

bool addNewTile()
{
  game.activeTile.y = 0;
  game.activeTile.x = (game.grid.x - 1) / 2;
  if (tileOccupied(game.activeTile))
    return false;
  newTile(game.activeTile);
  return true;
}

bool moveRight()
{
  coord const newTile = {game.activeTile.x + 1, game.activeTile.y};
  if (game.activeTile.x < (game.grid.x - 1) && !tileOccupied(newTile))
  {
    copyTile(newTile, game.activeTile);
    resetTile(game.activeTile);
    game.activeTile = newTile;
    return true;
  }
  return false;
}

bool moveLeft()
{
  coord const newTile = {game.activeTile.x - 1, game.activeTile.y};
  if (game.activeTile.x > 0 && !tileOccupied(newTile))
  {
    copyTile(newTile, game.activeTile);
    resetTile(game.activeTile);
    game.activeTile = newTile;
    return true;
  }
  return false;
}

bool moveDown()
{
  coord const newTile = {game.activeTile.x, game.activeTile.y + 1};
  if (game.activeTile.y < (game.grid.y - 1) && !tileOccupied(newTile))
  {
    copyTile(newTile, game.activeTile);
    resetTile(game.activeTile);
    game.activeTile = newTile;
    return true;
  }
  return false;
}

bool clearRow()
{
  if (rowOccupied(game.grid.y - 1))
  {
    for (unsigned int y = game.grid.y - 1; y > 0; y--)
    {
      copyRow(y, y - 1);
    }
    resetRow(0);
    return true;
  }
  return false;
}

void advanceLevel()
{
  game.level++;
  switch (game.nextGameTick)
  {
  case 1:
    break;
  case 2 ... 10:
    game.nextGameTick--;
    break;
  case 11 ... 20:
    game.nextGameTick -= 2;
    break;
  default:
    game.nextGameTick -= 10;
  }
}

void newGame()
{
  game.state = ACTIVE;
  game.tiles = 0;
  game.rows = 0;
  game.score = 0;
  game.tick = 0;
  game.level = 0;
  resetPlayfield();
}

void gameOver()
{
  game.state = GAMEOVER;
  game.nextGameTick = game.initNextGameTick;
}

bool sTetris(int const key)
{
  bool playfieldChanged = false;

  if (game.state & ACTIVE)
  {
    // Move the current tile
    if (key)
    {
      playfieldChanged = true;
      switch (key)
      {
      case KEY_LEFT:
        moveLeft();
        break;
      case KEY_RIGHT:
        moveRight();
        break;
      case KEY_DOWN:
        while (moveDown())
        {
        };
        game.tick = 0;
        break;
      default:
        playfieldChanged = false;
      }
    }

    // If we have reached a tick to update the game
    if (game.tick == 0)
    {
      // We communicate the row clear and tile add over the game state
      // clear these bits if they were set before
      game.state &= ~(ROW_CLEAR | TILE_ADDED);

      playfieldChanged = true;
      // Clear row if possible
      if (clearRow())
      {
        game.state |= ROW_CLEAR;
        game.rows++;
        game.score += game.level + 1;
        if ((game.rows % game.rowsPerLevel) == 0)
        {
          advanceLevel();
        }
      }

      // if there is no current tile or we cannot move it down,
      // add a new one. If not possible, game over.
      if (!tileOccupied(game.activeTile) || !moveDown())
      {
        if (addNewTile())
        {
          game.state |= TILE_ADDED;
          game.tiles++;
        }
        else
        {
          gameOver();
        }
      }
    }
  }

  // Press any key to start a new game
  if ((game.state == GAMEOVER) && key)
  {
    playfieldChanged = true;
    newGame();
    addNewTile();
    game.state |= TILE_ADDED;
    game.tiles++;
  }

  return playfieldChanged;
}

int readKeyboard()
{
  struct pollfd pollStdin = {
      .fd = STDIN_FILENO,
      .events = POLLIN};
  int lkey = 0;

  if (poll(&pollStdin, 1, 0))
  {
    lkey = fgetc(stdin);
    if (lkey != 27)
      goto exit;
    lkey = fgetc(stdin);
    if (lkey != 91)
      goto exit;
    lkey = fgetc(stdin);
  }
exit:
  switch (lkey)
  {
  case 10:
    return KEY_ENTER;
  case 65:
    return KEY_UP;
  case 66:
    return KEY_DOWN;
  case 67:
    return KEY_RIGHT;
  case 68:
    return KEY_LEFT;
  }
  return 0;
}

void renderConsole(bool const playfieldChanged)
{
  if (!playfieldChanged)
    return;

  // Goto beginning of console
  fprintf(stdout, "\033[%d;%dH", 0, 0);
  for (unsigned int x = 0; x < game.grid.x + 2; x++)
  {
    fprintf(stdout, "-");
  }
  fprintf(stdout, "\n");
  for (unsigned int y = 0; y < game.grid.y; y++)
  {
    fprintf(stdout, "|");
    for (unsigned int x = 0; x < game.grid.x; x++)
    {
      coord const checkTile = {x, y};
      fprintf(stdout, "%c", (tileOccupied(checkTile)) ? '#' : ' ');
    }
    switch (y)
    {
    case 0:
      fprintf(stdout, "| Tiles: %10u\n", game.tiles);
      break;
    case 1:
      fprintf(stdout, "| Rows:  %10u\n", game.rows);
      break;
    case 2:
      fprintf(stdout, "| Score: %10u\n", game.score);
      break;
    case 4:
      fprintf(stdout, "| Level: %10u\n", game.level);
      break;
    case 7:
      fprintf(stdout, "| %17s\n", (game.state == GAMEOVER) ? "Game Over" : "");
      break;
    default:
      fprintf(stdout, "|\n");
    }
  }
  for (unsigned int x = 0; x < game.grid.x + 2; x++)
  {
    fprintf(stdout, "-");
  }
  fflush(stdout);
}

inline unsigned long uSecFromTimespec(struct timespec const ts)
{
  return ((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000));
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  // This sets the stdin in a special state where each
  // keyboard press is directly flushed to the stdin and additionally
  // not outputted to the stdout
  {
    struct termios ttystate;
    tcgetattr(STDIN_FILENO, &ttystate);
    ttystate.c_lflag &= ~(ICANON | ECHO);
    ttystate.c_cc[VMIN] = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
  }

  // Allocate the playing field structure
  game.rawPlayfield = (tile *)malloc(game.grid.x * game.grid.y * sizeof(tile));
  game.playfield = (tile **)malloc(game.grid.y * sizeof(tile *));
  if (!game.playfield || !game.rawPlayfield)
  {
    fprintf(stderr, "ERROR: could not allocate playfield\n");
    return 1;
  }
  for (unsigned int y = 0; y < game.grid.y; y++)
  {
    game.playfield[y] = &(game.rawPlayfield[y * game.grid.x]);
  }

  // Reset playfield to make it empty
  resetPlayfield();
  // Start with gameOver
  gameOver();

  u_int16_t *fb_data;
  if (!initializeSenseHat())
  {
    fprintf(stderr, "ERROR: could not initilize sense hat\n");
    return 1;
  };

  // Clear console, render first time
  fprintf(stdout, "\033[H\033[J");
  renderConsole(true);
  renderSenseHatMatrix(true);

  while (true)
  {
    struct timeval sTv, eTv;
    gettimeofday(&sTv, NULL);

    int key = readSenseHatJoystick();
    if (!key)
      key = readKeyboard();
    if (key == KEY_ENTER)
      break;

    bool playfieldChanged = sTetris(key);
    renderConsole(playfieldChanged);
    renderSenseHatMatrix(playfieldChanged);

    // Wait for next tick
    gettimeofday(&eTv, NULL);
    unsigned long const uSecProcessTime = ((eTv.tv_sec * 1000000) + eTv.tv_usec) - ((sTv.tv_sec * 1000000 + sTv.tv_usec));
    if (uSecProcessTime < game.uSecTickTime)
    {
      usleep(game.uSecTickTime - uSecProcessTime);
    }
    game.tick = (game.tick + 1) % game.nextGameTick;
  }

  freeSenseHat();
  free(game.playfield);
  free(game.rawPlayfield);

  // I found that the terminal behaviour was not
  // restored on pressing enter, but it was on ctrl-c
  // this was the easiest method I found for solving it
  raise(SIGINT);
}