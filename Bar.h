#define BAR_WIDTH       80
#define BAR_HEIGHT      5
#define A_REGION_WIDTH  10
#define S_REGION_WIDTH  10
#define N_REGION_WIDTH  40

#define JUMP_SPEED  25  //This is the rate of positional change in terms of pixels per jump
#define MOVE_SPEED  200 //TODO: adjust this metric to pixels per second, depending on the desired framerate
#define  BAR_SIZE    8;

typedef enum{
    BAR_NO_MOVEMENT,
    BAR_MOVE_LEFT,
    BAR_MOVE_RIGHT,
    BAR_JUMP_LEFT,
    BAR_JUMP_RIGHT
} BarMovementCode;

struct Bar_s{
    int x; /*horizontal anchor*/
    const int y; /*vertical anchor*/
} Bar_default = {0, 0};

typedef struct Bar_s Bar;

void updateBar(Bar* bar, BarMovementCode);
