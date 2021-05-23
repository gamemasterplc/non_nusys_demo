#pragma once

//Defines amount of memory for local variables in a thread
#define MAIN_STACK_SIZE 0x2000
#define IDLE_STACK_SIZE 0x800

//Define thread priorities
//These define which threads are most critical to run
//These should all be less than 127 since anything greater is reserved
#define MAIN_PRIORITY 10
#define AUDIO_PRIORITY 70
#define SCHED_PRIORITY 120

#define NUM_GFX_MSGS 8
#define NUM_PI_MSGS 16
#define NUM_SI_MSGS 4

//Define resolution of program
//Generates preprocessor error if not 320x240 or 640x480
#define SCREEN_WD 320
#define SCREEN_HT 240

//Place framebuffers at end of RAM
#define CFB1_ADDR (0x80400000-(SCREEN_WD*SCREEN_HT*2*2))
#define CFB2_ADDR (0x80400000-(SCREEN_WD*SCREEN_HT*2))

//Size of display list buffer
#define GLIST_SIZE 2048

//Size of RSP FIFO for F3DEX2
#define FIFO_SIZE 8192

//Square Render Properties
#define SQUARE_SIZE 32
#define SQUARE_BORDER_W 2

//Audio heap definitions
//Placed immediately before framebuffers in this demo
#define AUDIO_HEAP_SIZE 0x60000
#define AUDIO_HEAP_ADDR (CFB1_ADDR-AUDIO_HEAP_SIZE)

//Define Size of audio data buffers
#define PTR_BUF_SIZE 8192
#define TUNE_BUF_SIZE 16384

//Define maximum chunk size for DMAs
//Used to prevent clashes with other DMAs
//Since the PI only supports one DMA at a time
#define DMA_BLOCK_SIZE 16384

//Square movement parameters
#define STICK_DEADZONE 10
#define SQUARE_VELOCITY_SCALE (1/24.0f)

//Expose audio data pointer to program
extern u8 pbank_start[];
extern u8 pbank_end[];
extern u8 wbank_start[];
extern u8 wbank_end[];
extern u8 sng_menu_start[];
extern u8 sng_menu_end[];