#include "primarycore.h"

XGpio gpPB; //PB device instance.
Bar bar = {0, FLOOR - 10 - 3};
Ball ball;
unsigned int cyclesElapsed;

static void gpPBIntHandler(void *arg)
{
    //clear the interrupt flag. if this is not done, gpio will keep interrupting the microblaze.--
    // --Possible to use (XGpio*)arg instead of &gpPB
    XGpio_InterruptClear(&gpPB,1);
    //Read the state of the push buttons.
    buttonInput = XGpio_DiscreteRead(&gpPB, 1);
    //TODO: configure bar movement codes for "jump"
    switch(buttonInput){
        case BUTTON_LEFT:
        barMovementCode = BAR_MOVE_LEFT;
        break;
        case BUTTON_RIGHT:
        barMovementCode = BAR_MOVE_RIGHT;
        break;
        default: //No movement if more than one button is pressed at a time.
        barMovementCode = BAR_NO_MOVEMENT;
    }
    if(buttonInput & BUTTON_CENTER){
        paused = !paused;
    }
}

//Firmware entry point
int main(void){
    xilkernel_init();
    xmk_add_static_thread(main_prog,0);
    xilkernel_start();
    xilkernel_main ();
    return 0;
}

//Xilkernel entry point
int main_prog(void){
	int status;
    /*BEGIN MAILBOX INITIALIZATION*/
    XMbox_Config *ConfigPtr;
    ConfigPtr = XMbox_LookupConfig(MBOX_DEVICE_ID);
    if(ConfigPtr == (XMbox_Config *)NULL){
        print("Error configuring mailbox uB1 Receiver\r\n");
        return XST_FAILURE;
    }

    status = XMbox_CfgInitialize(&mailbox, ConfigPtr, ConfigPtr->BaseAddress);
    if (status != XST_SUCCESS) {
        print("Error initializing mailbox uB1 Receiver--\r\n");
        return XST_FAILURE;
    }
    /*END MAILBOX INITIALIZATION*/

    /*BEGIN INTERRUPT CONFIGURATION*/
    // Initialise the PB instance
    status = XGpio_Initialize(&gpPB, XPAR_GPIO_0_DEVICE_ID);
    if (status == XST_DEVICE_NOT_FOUND) {
        safePrint("ERROR initializing XGpio: Device not found");
    }
    // set PB gpio direction to input.
    XGpio_SetDataDirection(&gpPB, 1, 0x000000FF);
    //global enable
    XGpio_InterruptGlobalEnable(&gpPB);
    // interrupt enable. both global enable and this function should be called to enable gpio interrupts.
    XGpio_InterruptEnable(&gpPB, 1);
    //register the handler with xilkernel
    register_int_handler(XPAR_MICROBLAZE_0_AXI_INTC_AXI_GPIO_0_IP2INTC_IRPT_INTR, gpPBIntHandler, &gpPB);
    //enable the interrupt in xilkernel
    enable_interrupt(XPAR_MICROBLAZE_0_AXI_INTC_AXI_GPIO_0_IP2INTC_IRPT_INTR);
    /*END INTERRUPT CONFIGURATION*/

    /*BEGIN TFT CONTROLLER INITIALIZATION*/
    status = TftInit(TFT_DEVICE_ID, &TftInstance);
    if ( status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    /*END TFT CONTROLLER INITIALIZATION*/

    /* BEGIN XMUTEX INITIALIZATION */
    status = initXMutex();
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    /* END XMUTEX INITIALIZATION */

    //Initialize thread semaphores
    if(sem_init(&sem_running, SEM_SHARED, SEM_BLOCKED)){
        safePrint("Error while initializing semaphore sem_running!\r\n");
        while(TRUE); //Trap runtime here
    }
    if(sem_init(&sem_drawGameArea, SEM_SHARED, SEM_BLOCKED)){
        safePrint("Error while initializing semaphore sem_drawGameArea!\r\n");
        while(TRUE); //Trap runtime here
    }
    if(sem_init(&sem_brickCollisionListener, SEM_SHARED, SEM_BLOCKED)){
        safePrint("Error while initializing semaphore sem_brickCollisionListener!\r\n");
        while(TRUE); //Trap runtime here
    }
    if(sem_init(&sem_mailboxListener, SEM_SHARED, SEM_BLOCKED)){
        safePrint("Error while initializing semaphore sem_mailboxListener!\r\n");
        while(TRUE); //Trap runtime here
    }
    if(sem_init(&sem_drawStatusArea, SEM_SHARED, SEM_BLOCKED)){
        safePrint("Error while initializing semaphore sem_drawStatusArea!\r\n");
        while(TRUE); //Trap runtime here
    }
    /*
    Thread priority (0 is highest priority):
    1. thread_mainLoop:
    • highest priority: preempts all other threads while not blocked (waiting for them to finish)
    2. thread_mailboxListener
    • If the mailbox is blocked, the second core also stalls.
    3. thread_brickCollisionListener
    • Update appropriate game values if the ball collided with a brick.
    4. thread_drawGameArea
    • FIXME: Possible issue: Drawing the game area is time-consuming and will require messagequeue usage. Messagequeue overflow?
    5. thread_drawStatusArea
    • Low-priority thread. After it is run, the frame is considered ready to be displayed.
    */
    pthread_attr_init(&attr);
    schedpar.sched_priority = PRIO_HIGHEST;
    pthread_attr_setschedparam(&attr,&schedpar);
    pthread_create(&pthread_mainLoop, &attr, (void*)thread_mainLoop, NULL);

    schedpar.sched_priority++;
    pthread_attr_setschedparam(&attr, &schedpar);
    pthread_create(&pthread_brickCollisionListener, &attr, (void*)thread_brickCollisionListener, NULL);

    schedpar.sched_priority++;
    pthread_attr_setschedparam(&attr, &schedpar);
    pthread_create(&pthread_drawGameArea, &attr, (void*)thread_drawGameArea, NULL);

    schedpar.sched_priority++;
    pthread_attr_setschedparam(&attr, &schedpar);
    pthread_create(&pthread_drawStatusArea, &attr, (void*)thread_drawStatusArea, NULL);

    schedpar.sched_priority++;
    pthread_attr_setschedparam(&attr, &schedpar);
    pthread_create(&pthread_mailboxListener, &attr, (void*)thread_mailboxListener, NULL);

    return 0;
}

//Game mainloop thread
void* thread_mainLoop(void){
    while(TRUE){
        //Welcome
        // safePrint("primarycore: Welcome\r\n");
        welcome();
        while(!(buttonInput & BUTTON_CENTER));//while(!start)
        sleep(1000); //FIXME: hardcoded delay
        //Running
        while(lives && !win){
            //Ready
            while(FALSE/*!(buttonInput & BUTTON_CENTER)*/){ //while(!launch) //FIXME: QUEUES MESSAGES, BUT NOTHING IS READING THEM
                // safePrint("primarycore: ready\r\n");
                ready();
                sleep(SLEEPCONSTANT); //FIXME: sleep calibration
            }

            //Running
            while(!win && !loseLife){
                if(!paused){
                    // safePrint("primarycore: running\r\n");
                    running();
                }
                else{
                    //Paused
                }
                sleep(SLEEPCONSTANT); //FIXME: sleep calibration
            }

            //Lost Life
            if(loseLife){
                loseLife = FALSE;
                lives--;
            }
        }
        if(!lives){
            //Game Over
            gameOver();
        }
        else{
            //Win
            gameWin();
        }
        //Wait for keypress to restart game
        while(!(buttonInput & BUTTON_CENTER)); //while(!restart)
    }
}

void welcome(void){
	int drawGameAreaBackground = 1;
    lives = INITIAL_LIVES;
    bar.x = (LEFT_WALL + RIGHT_WALL)/2;
    //FIXME: hacky fix with the bar.x1
   queueMsg(MSGQ_TYPE_GAMEAREA, &drawGameAreaBackground, MSGQ_MSGSIZE_GAMEAREA);
   queueMsg(MSGQ_TYPE_STATUSAREA, &drawGameAreaBackground, MSGQ_MSGSIZE_STATUSAREA);

    cyclesElapsed = 0;

    queueMsg(MSGQ_TYPE_BAR, &bar, MSGQ_MSGSIZE_BAR);
    // ball = Ball_default; //FIXME: restore Ball_default
    ball.x = bar.x;
    ball.y = BAR_Y - DIAMETER / 2;
    ball.d = 340;
    ball.s = 10;
    ball.c = 0;
    queueMsg(MSGQ_TYPE_BALL, &ball, MSGQ_MSGSIZE_BALL);

    //Send a message to the secondary core, signaling a restart
    //The secondary core should reply with a draw message for every brick
    MBOX_MSG_TYPE restartMessage = MBOX_MSG_RESTART;
    int dataBuffer[MBOX_MSG_BEGIN_COMPUTATION_SIZE];
    buildBallMessage(&ball, dataBuffer);
    XMbox_WriteBlocking(&mailbox, (u32*)&restartMessage, MBOX_MSG_ID_SIZE);
    XMbox_WriteBlocking(&mailbox, (u32*)dataBuffer, MBOX_MSG_BEGIN_COMPUTATION_SIZE);
    //Receive brick information and draw everything on screen.
    // sem_post(&sem_drawGameArea);safePrint("Line 206\r\n");
    sem_post(&sem_mailboxListener);
    // sem_post(&sem_brickCollisionListener);safePrint("Line 208\r\n");
    //Wait for the three branched threads to finish, regardless of the order.
    sem_wait(&sem_running);

    //Draw the status area
    sem_post(&sem_drawStatusArea);
    //Wait for the drawing operation to complete.
    sem_wait(&sem_running);
    //TODO: draw welcome text
}

void ready(void){
    //Erase the bar
    bar.c = GAMEAREA_COLOR;
    queueMsg(MSGQ_TYPE_BAR, &bar, MSGQ_MSGSIZE_BAR);
    bar.c = COLOR_NONE;
    //Erase the ball
    ball.c = GAMEAREA_COLOR;
    queueMsg(MSGQ_TYPE_BALL, &ball, MSGQ_MSGSIZE_BALL);
    ball.c = COLOR_NONE;

    updateBar(&bar, barMovementCode);
    followBar(&ball, &bar);
    //FIXME: clear previous bar and ball before redrawing.
    queueMsg(MSGQ_TYPE_BAR, &bar, MSGQ_MSGSIZE_BAR);
    queueMsg(MSGQ_TYPE_BALL, &ball, MSGQ_MSGSIZE_BALL);
}

void queueMsg(const MSGQ_TYPE msgType, void* data, const MSGQ_MSGSIZE size){
    int msgid;
    msgid = msgget(msgType, IPC_CREAT);

    if( msgid == -1 ) {
        xil_printf ("Error while queueing draw data. MSG_TYPE:%d\tsize:%d\tErrno: %d\r\n", msgType, size, errno);
        pthread_exit (&errno);
    }
    if(msgsnd(msgid, data, size, 0) < 0 ) { // blocking send
        xil_printf ("Msgsnd of message(%d) ran into ERROR. Errno: %d. Halting..\r\n", msgType, errno);
        pthread_exit(&errno);
    }
}

void running(void){
    int drawGameAreaBackground = 1;
    //Erase the bar
    bar.c = GAMEAREA_COLOR;
    queueMsg(MSGQ_TYPE_BAR, &bar, MSGQ_MSGSIZE_BAR);
    bar.c = COLOR_NONE;
    //Erase the ball
    ball.c = GAMEAREA_COLOR;
    queueMsg(MSGQ_TYPE_BALL, &ball, MSGQ_MSGSIZE_BALL);
    ball.c = COLOR_NONE;

    updateBar(&bar, barMovementCode);
    updateBallPosition(&ball);
    queueMsg(MSGQ_TYPE_BAR, &bar, MSGQ_MSGSIZE_BAR);
    queueMsg(MSGQ_TYPE_BALL, &ball, MSGQ_MSGSIZE_BALL);
    //queueMsg(MSGQ_TYPE_GAMEAREA, &drawGameAreaBackground, MSGQ_MSGSIZE_GAMEAREA);

    //Check collision with walls
    updateBallDirection(&ball, checkCollideWall(&ball));

    //Check collision with bar
    updateBallDirection(&ball, checkCollideBar(&ball, &bar));

    unsigned int message[MBOX_MSG_BEGIN_COMPUTATION_SIZE];
    buildBallMessage(&ball, message);
    //Send the ball position to the secondary core to initialize collision checking
    XMbox_WriteBlocking(&mailbox, (u32*) message, MBOX_MSG_BEGIN_COMPUTATION_SIZE);
    if(cyclesElapsed++ >= GOLDEN_COLUMN_CHANGE_CONSTANT){
        safePrint("Update golden!\r\n");
        cyclesElapsed = 0;
        message[0] = MBOX_MSG_UPDATE_GOLDEN;
        XMbox_WriteBlocking(&mailbox, (u32*) message, MBOX_MSG_UPDATE_GOLDEN_SIZE);
        XMbox_WriteBlocking(&mailbox, (u32*) message, MBOX_MSG_UPDATE_GOLDEN_SIZE);
    }
    //Receive brick information and draw everything on screen.
    sem_post(&sem_mailboxListener);
    //Wait for the three branched threads to finish
    sem_wait(&sem_running);

    //Draw the status area
    sem_post(&sem_drawStatusArea);
    //Wait for the drawing operation to complete.
    sem_wait(&sem_running);
}

inline void buildBallMessage(Ball* ball, unsigned int* message){
    message[0] = MBOX_MSG_BEGIN_COMPUTATION;
    message[1] = ball->x;
    message[2] = ball->y;
}

//Receives messagequeue messages
//TODO: split into separate draw methods:ball, bar, brick, background
void* thread_drawGameArea(void){
    unsigned int dataBuffer[3];
    while(TRUE){
        sem_wait(&sem_drawGameArea); //Wait to be signaled
        // while(readFromMessageQueue(MSGQ_TYPE_BACKGROUND, dataBuffer, MSGQ_MSGSIZE_BACKGROUND)){
        //     safePrint("primarycore: drawBackground\r\n");
        //     draw(dataBuffer, MSGQ_TYPE_BACKGROUND);
        // }
        while(readFromMessageQueue(MSGQ_TYPE_GAMEAREA, dataBuffer, MSGQ_MSGSIZE_GAMEAREA)){
            // safePrint("primarycore: drawBackground\r\n");
            draw(dataBuffer, MSGQ_TYPE_GAMEAREA);
        }
        while(readFromMessageQueue(MSGQ_TYPE_BAR, dataBuffer, MSGQ_MSGSIZE_BAR)){
            // safePrint("primarycore: drawBar\r\n");
            draw(dataBuffer, MSGQ_TYPE_BAR);
        }
        while(readFromMessageQueue(MSGQ_TYPE_BALL, dataBuffer, MSGQ_MSGSIZE_BALL)){
            // safePrint("primarycore: drawBall\r\n");
            draw(dataBuffer, MSGQ_TYPE_BALL);
        }
        while(readFromMessageQueue(MSGQ_TYPE_BRICK, dataBuffer, MSGQ_MSGSIZE_BRICK)){
            // safePrint("primarycore: drawBrick\r\n");
            draw(dataBuffer, MSGQ_TYPE_BRICK);
        }
    }
}

int readFromMessageQueue(const MSGQ_TYPE id, void* dataBuffer, const MSGQ_MSGSIZE size){
    int msgid = msgget (id, IPC_CREAT);
    int msgSize;
    if( msgid == -1 ) {
        xil_printf ("receiveMessage -- ERROR while opening Message Queue. Errno: %d \r\n", errno);
        pthread_exit(&errno) ;
    }
    msgSize = msgrcv(msgid, dataBuffer, size, 0, IPC_NOWAIT);
    if(msgSize != size) { // NON-BLOCKING receive
        if(errno == EAGAIN){return FALSE;}
        xil_printf ("receiveMessage of message(%d) ran into ERROR. Errno: %d. Halting...\r\n", id, errno);
        pthread_exit(&errno);
    }
    return TRUE;
}

//Receives messagequeue messages
void* thread_brickCollisionListener(void){
    unsigned int dataBuffer[3];
    while(TRUE){
        sem_wait(&sem_brickCollisionListener);
            //TODO: remove the while loop. no point in it :P
            while(readFromMessageQueue(MSGQ_TYPE_BRICK_COLLISION, dataBuffer, MSGQ_MSGSIZE_BRICK_COLLISION)){
//                if(increaseScore(dataBuffer[1])){ //FIXME: magic numbers when interpreting the data buffer
//                    increaseSpeed(ball);
//                }
                updateBallDirection(&ball, dataBuffer[0]); //TODO: implement method. dataBuffer[0] should be a CollisionCodeType
            }
        // sem_post(&sem_running); //Signal the running thread that we're done.
    }
}

//Receives mailbox messages from the secondary core
void* thread_mailboxListener(void){
    Brick brick;
    MBOX_MSG_TYPE msgType;
    unsigned int dataBuffer[3]; //FIXME: magic numbers when declaring array size
    while(TRUE){
        sem_wait(&sem_mailboxListener);
        brickUpdateComplete = FALSE;
        while(!brickUpdateComplete){
            XMbox_ReadBlocking(&mailbox, (u32*)&msgType, MBOX_MSG_ID_SIZE);
            switch(msgType){
                case MBOX_MSG_DRAW_BRICK:
                    XMbox_ReadBlocking(&mailbox, (u32*)dataBuffer, MBOX_MSG_DRAW_BRICK_SIZE);
                    queueMsg(MSGQ_TYPE_BRICK, dataBuffer, MSGQ_MSGSIZE_BRICK);
                    sem_post(&sem_drawGameArea); //TODO: fix semaphore when drawGameArea has been split into several methods
                    break;
                case MBOX_MSG_COLLISION:
                    XMbox_ReadBlocking(&mailbox, (u32*)dataBuffer, MBOX_MSG_COLLISION_SIZE);
                    queueMsg(MSGQ_TYPE_BRICK_COLLISION, dataBuffer, MSGQ_MSGSIZE_BRICK_COLLISION);
                    sem_post(&sem_brickCollisionListener);
                    break;
                case MBOX_MSG_COMPUTATION_COMPLETE:
                    brickUpdateComplete = TRUE;
                    break;
                default:
                    while(TRUE); //This should not happen. Trap runtime!
            }
        }
        sem_post(&sem_running); //Signal the running thread that we're done.
    }
}

void* thread_drawStatusArea(void){
    while(TRUE){
        sem_wait(&sem_drawStatusArea);
        draw(0, MSGQ_TYPE_STATUSAREA);
        //TODO: draw the status area
        sem_post(&sem_running); //Signal the running thread that we're done.
    }
}

//FIXME: For the drawing of the bar, replace else if chain with separate for loops for each section (or other fix)
void draw(unsigned int* dataBuffer, const MSGQ_TYPE msgType){
    int i, j;
    int x, y, c;
    switch(msgType){
        case MSGQ_TYPE_BRICK:
            //FIXME: hardcoded indexes
            x = dataBuffer[0];
            y = dataBuffer[1];
            c = dataBuffer[2];

            for(j = y - BRICK_HEIGHT/2; j < y + BRICK_HEIGHT/2; j++) {
                for(i = x - BRICK_WIDTH/2; i < x + BRICK_WIDTH/2; i++) {
                    XTft_SetPixel(&TftInstance, i, j, c);
                }
            }


            break;

        case MSGQ_TYPE_BAR:
            //FIXME: hardcoded indexes
            x = dataBuffer[0];
            y = dataBuffer[1];
            c = dataBuffer[2];

            for(j = y - BAR_HEIGHT/2; j < y + BAR_HEIGHT/2; j++) {
                for(i = 0; i < BAR_WIDTH; i++) {
                    if(i < A_REGION_WIDTH) {
                        XTft_SetPixel(&TftInstance, i + x - BAR_WIDTH/2, j, c ? c : A_REGION_COLOR);
                    } else if(i < A_REGION_WIDTH + S_REGION_WIDTH) {
                        XTft_SetPixel(&TftInstance, i + x - BAR_WIDTH/2, j, c ? c : S_REGION_COLOR);
                    } else if(i < A_REGION_WIDTH + S_REGION_WIDTH + N_REGION_WIDTH) {
                        XTft_SetPixel(&TftInstance, i + x - BAR_WIDTH/2, j, c ? c : N_REGION_COLOR);
                    } else if(i < A_REGION_WIDTH + 2*S_REGION_WIDTH + N_REGION_WIDTH) {
                        XTft_SetPixel(&TftInstance, i + x - BAR_WIDTH/2, j, c ? c : S_REGION_COLOR);
                    }else{
                        XTft_SetPixel(&TftInstance, i + x - BAR_WIDTH/2, j, c ? c : A_REGION_COLOR);
                    }
                }
            }

            break;

        case MSGQ_TYPE_BALL:
            //FIXME: hardcoded indexes
            x = dataBuffer[0];
            y = dataBuffer[1];
            c = dataBuffer[2];
            for(j = 0; j < DIAMETER; j++) {
                for (i = 0; i < DIAMETER; i++) {
                    if(BALL_MASK[j][i] == 0xFFFFFFFF) {
                        XTft_SetPixel(&TftInstance, x - DIAMETER/2 + i, y - DIAMETER/2 + j, c ? c : BALL_COLOR);
                    }
                }
            }
            break;

        case MSGQ_TYPE_BACKGROUND:
            //Fill screen with background color
            for(j = 0; j < 479; j++) {
                for(i = 0; i < 639; i++) {
                    XTft_SetPixel(&TftInstance, i, j, BACKGROUND_COLOR);
                }
            }
            break;

        case MSGQ_TYPE_GAMEAREA:
            //Draw game area
            for(j = CEIL; j < FLOOR; j++) {
                for(i = LEFT_WALL; i < RIGHT_WALL; i++) {
                    XTft_SetPixel(&TftInstance, i, j, GAMEAREA_COLOR);
                }
            }
            break;

        case MSGQ_TYPE_STATUSAREA:
            //Draw score area
            for(j = SCORE_CEIL; j < SCORE_FLOOR; j++) {
                for(i = SCORE_LEFT_WALL; i < SCORE_RIGHT_WALL; i++) {
                    XTft_SetPixel(&TftInstance, i, j, STATUSAREA_COLOR);
                }
            }
            break;

        default:
        	while(TRUE); //Should never happen. Trap runtime here.
    }
}

//TODO: implement gameOver
//GameOver method should display "Game Over" text and prompt the user to press a key to restart
void gameOver(void){

}

//TODO: implement win
//Win method should display "Win" text and prompt the user to press a key to restart
void gameWin(void){

}
