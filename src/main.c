#include <ultra64.h>
#include <PR/sched.h>
#include <PR/libmus.h>
#include "main.h"

//Define stacks
//Main stack must not be static to export to entrypoint code
u64 main_stack[MAIN_STACK_SIZE/8];
static u64 idle_stack[IDLE_STACK_SIZE/8];
static u64 sched_stack[OS_SC_STACKSIZE/8];

//Define OSSched scheduler instance
static OSSched scheduler;

//Define main and idle threads
static OSThread idle_thread;
static OSThread main_thread;

//Define message queues and message buffers
static OSMesgQueue pi_msg_queue;
static OSMesg pi_msgs[NUM_PI_MSGS];
static OSMesgQueue gfx_msg_queue;
static OSMesg gfx_msgs[NUM_GFX_MSGS];
static OSMesgQueue si_msg_queue;
static OSMesg si_msgs[NUM_SI_MSGS];

//Define PI Handle
//The handle is used to read from ROM with the EPI interface
static OSPiHandle *cart_handle;

//Define display list buffer
static Gfx glist[GLIST_SIZE];
//Keep track of position in display list buffer
static Gfx *glistp;

//Define 16-Byte aligned F3DEX2 buffers
//Define F3DEX2 matrix stack variable
static u64 dram_stack[SP_DRAM_STACK_SIZE64] __attribute__((aligned(16)));
//Buffer to hold results of F3DEX2 display list processing
static u64 fifo_buf[FIFO_SIZE/8] __attribute__((aligned(16)));
//Buffer to allow interruption of F3DEX2 by audio microcode
static u64 yield_buf[OS_YIELD_DATA_SIZE/8] __attribute__((aligned(16)));

//Define libmus audio data buffers
static u8 ptr_buf[PTR_BUF_SIZE] __attribute__((aligned(4)));
static u8 tune_buf[TUNE_BUF_SIZE] __attribute__((aligned(4)));

//Store controller data
static OSContStatus cont_statuses[MAXCONTROLLERS];
static OSContPad cont_data[MAXCONTROLLERS];

static void idle(void *arg);
static void main(void *arg);

//Start up game
//This function must not be static to be seen by entrypoint code
void boot()
{
	//Start up OS
	osInitialize();
	//Start up an idle thread
	osCreateThread(&idle_thread, 1, idle, NULL, &idle_stack[IDLE_STACK_SIZE/8], MAIN_PRIORITY);
	osStartThread(&idle_thread);
}

static void idle(void *arg)
{
	//Initialize PI to read cart
	osCreatePiManager(OS_PRIORITY_PIMGR, &pi_msg_queue, pi_msgs, NUM_PI_MSGS);
	cart_handle = osCartRomInit();
	//Start up main thread
	osCreateThread(&main_thread, 3, main, NULL, &main_stack[MAIN_STACK_SIZE/8], MAIN_PRIORITY);
	osStartThread(&main_thread);
	//Make this the idle thread
	osSetThreadPri(NULL, 0);
	//Busy wait
	while(1);
}

static void ReadRom(u32 src, void *dst, u32 size)
{
	OSIoMesg io_mesg;
    OSMesgQueue dma_msg_queue;
    OSMesg dma_msg;
    u32 read_size;
	//Use alternative variable for dst
	u8 *dma_dest = (u8 *)dst;
	
	//Initialize DMA message queue for waiting for DMA to be done
    osCreateMesgQueue(&dma_msg_queue, &dma_msg, 1);
	
	//Setup static parameters in OSIoMesg for ROM read
    io_mesg.hdr.pri = OS_MESG_PRI_NORMAL;
    io_mesg.hdr.retQueue = &dma_msg_queue;
	
	//Invalidate data cache DMA will write to
    osInvalDCache(dma_dest, size);
	
	//DMA in chunks
    while(size){
		//Determine read size based off the size left to read
		if(size > DMA_BLOCK_SIZE){
			//Cap read size if size is greater than block size
			read_size = DMA_BLOCK_SIZE;
		} else {
			//Read size is identical to size remaining
			read_size = size;
		}
		//Setup dynamic parameters in OSIoMesg for ROM read
		io_mesg.dramAddr = dma_dest;
		io_mesg.devAddr = src;
		io_mesg.size = read_size;
		
		//Start reading from ROM
		osEPiStartDma(cart_handle, &io_mesg, OS_READ);
		//Wait for ROM Read to be done
		osRecvMesg(&dma_msg_queue, &dma_msg, OS_MESG_BLOCK);
		
		//Advance pointers for next ROM read
		src += read_size;
		dma_dest += read_size;
		size -= read_size;
    }
}

static void InitAudio()
{
	musConfig config;
	config.control_flag = 0; //Use ROM wavetables
	config.channels = 24; //Decent default for channel count
	config.sched = &scheduler; //Point to existing OSSched scheduler instance
	config.thread_priority = AUDIO_PRIORITY;
	//Setup audio heap
	config.heap = (u8 *)AUDIO_HEAP_ADDR;
	config.heap_length = AUDIO_HEAP_SIZE;
	//Set FIFO length to minimum
	config.fifo_length = 64;
	//Assign no initial audio or FX bank
	config.ptr = NULL;
	config.wbk = NULL;
	config.default_fxbank = NULL;
	//Set audio default frequency
	config.syn_output_rate = 44100;
	//Set synthesizer parameters to sane defaults from nualstl3
	config.syn_updates = 256;
	config.syn_rsp_cmds = 2048;
	config.syn_num_dma_bufs = 64;
	config.syn_dma_buf_size = 1024;
	config.syn_retraceCount = 1; //Must be same as last parameter to osCreateScheduler
	//Initialize libmus
	MusInitialize(&config);
	//Read and initialize song sample bank
	ReadRom((u32)pbank_start, ptr_buf, pbank_end-pbank_start);
    MusPtrBankInitialize(ptr_buf, wbank_start);
	//Read and play back the song
	ReadRom((u32)sng_menu_start, tune_buf, sng_menu_end-sng_menu_start);
	MusStartSong(tune_buf);
}

static void InitController()
{
	//Variable to hold bit pattern of controllers plugged in
	u8 pattern;
	//Initialize SI message queue
	osCreateMesgQueue(&si_msg_queue, si_msgs, NUM_SI_MSGS);
	//Register SI message queue with SI events
	osSetEventMesg(OS_EVENT_SI, &si_msg_queue, NULL);
	//Initialize SI devices
	osContInit(&si_msg_queue, &pattern, cont_statuses);
}

static void ReadController()
{
	//Start controller read
	osContStartReadData(&si_msg_queue);
	//Wait for controller read to finish
	osRecvMesg(&si_msg_queue, NULL, OS_MESG_BLOCK);
	//Get controller read data
	osContGetReadData(cont_data);
}

static void DrawRect(s32 x, s32 y, s32 w, s32 h, u16 color)
{
	//Don't draw fully offscreen rectangles
	if(x < -w || y < -h) {
		return;
	}
	//Clamp partially offscreen rectangles to screen
	if(x < 0) {
		w += x;
		x = 0;
	}
	if(y < 0) {
		h += y;
		y = 0;
	}
	//Set rectangle color
	gDPSetFillColor(glistp++, (color << 16)|color);
	//Draw rectangle
	gDPFillRectangle(glistp++, x, y, x+w-1, y+h-1);
	//Wait for rectangle to draw
	gDPPipeSync(glistp++);
}

static void main(void *arg)
{
	//Information to keep track of using two framebuffers
	int cfb_idx = 0;
	void *cfb_table[2] = { (void *)CFB1_ADDR, (void *)CFB2_ADDR };
	float square_pos_x, square_pos_y; //Square position
	OSScMsg done_msg; //Hold message for rendering being done
	OSScMsg *wait_msg; //Point to received messages
	OSScClient client; //Used for frame synchronization
	OSScTask sc_task; //Graphics task information
	
	//Initialize scheduler
	#if SCREEN_WD == 320 && SCREEN_HT == 240
	osCreateScheduler(&scheduler, &sched_stack[OS_SC_STACKSIZE/8], SCHED_PRIORITY, OS_VI_NTSC_LAN1, 1);
	#elif SCREEN_WD == 640 && SCREEN_HT == 480
	osCreateScheduler(&scheduler, &sched_stack[OS_SC_STACKSIZE/8], SCHED_PRIORITY, OS_VI_NTSC_HAN1, 1);
	#else
	#error "Invalid Resolution"
	#endif
	
	done_msg.type = OS_SC_DONE_MSG; //Register done message
	//Initialize graphics messages
	osCreateMesgQueue(&gfx_msg_queue, gfx_msgs, NUM_GFX_MSGS);
	osScAddClient(&scheduler, &client, &gfx_msg_queue);
	
	//Initialize other system shit
	InitController();
	InitAudio();
	
	//Initialize static part of task
	sc_task.list.t.type = M_GFXTASK; //This task is a graphics task
	sc_task.list.t.flags = 0; //Set to zero for FIFO graphics tasks
	//Set boot microcode to rspboot
	sc_task.list.t.ucode_boot = (u64 *)rspbootTextStart;
	sc_task.list.t.ucode_boot_size = ((u32)rspbootTextEnd - (u32)rspbootTextStart);
	sc_task.list.t.ucode = (u64 *)gspF3DEX2_fifoTextStart;
	//Set microcode to FIFO F3DEX2
	sc_task.list.t.ucode_size = SP_UCODE_SIZE;
	sc_task.list.t.ucode_data = (u64 *)gspF3DEX2_fifoDataStart;
	sc_task.list.t.ucode_data_size = SP_UCODE_DATA_SIZE;
	//Initialize DRAM stack parameters
	sc_task.list.t.dram_stack = dram_stack;
	sc_task.list.t.dram_stack_size = SP_DRAM_STACK_SIZE8;
	//Initialize output buffer parameters
	sc_task.list.t.output_buff = fifo_buf;
	sc_task.list.t.output_buff_size = &fifo_buf[FIFO_SIZE/8]; //Points to end of FIFO buffer for FIFO F3DEX2
	//Initialize yield buffer parameters
	sc_task.list.t.yield_data_ptr = yield_buf;
	sc_task.list.t.yield_data_size = OS_YIELD_DATA_SIZE;
	sc_task.next = NULL; //Unneeded
	//Set flags for this being the only task
	sc_task.flags = OS_SC_NEEDS_RSP | OS_SC_NEEDS_RDP | OS_SC_LAST_TASK | OS_SC_SWAPBUFFER;
	//Parameters for task done message
	sc_task.msgQ = &gfx_msg_queue;
	sc_task.msg = (OSMesg)&done_msg;
	
	//Center Square
	square_pos_x = (SCREEN_WD/2)-(SQUARE_SIZE/2);
	square_pos_y = (SCREEN_HT/2)-(SQUARE_SIZE/2);
	while(1) {
		//Update controller
		ReadController();
		//Move square with analog stick
		if(cont_data[0].stick_x > STICK_DEADZONE || cont_data[0].stick_x < -STICK_DEADZONE) {
			square_pos_x += cont_data[0].stick_x*SQUARE_VELOCITY_SCALE;
		}
		if(cont_data[0].stick_y > STICK_DEADZONE || cont_data[0].stick_y < -STICK_DEADZONE) {
			square_pos_y -= cont_data[0].stick_y*SQUARE_VELOCITY_SCALE;
		}
		//Keep square inside screen
		if(square_pos_x < 0) {
			square_pos_x = 0;
		}
		if(square_pos_x >= SCREEN_WD-SQUARE_SIZE) {
			square_pos_x = SCREEN_WD-SQUARE_SIZE-1;
		}
		if(square_pos_y < 0) {
			square_pos_y = 0;
		}
		if(square_pos_y >= SCREEN_HT-SQUARE_SIZE) {
			square_pos_y = SCREEN_HT-SQUARE_SIZE-1;
		}
		//Reset display list pointer
		glistp = glist;
		//Set up direct RAM mapping for segment zero
		gSPSegment(glistp++, 0, 0);
		//Enable drawing to whole screen
		gDPSetScissor(glistp++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WD, SCREEN_HT);
		//Set up filling rectangles
		gDPSetCycleType(glistp++, G_CYC_FILL);
		//Set rendering framebuffer
		gDPSetColorImage(glistp++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WD, cfb_table[cfb_idx]);
		//Draw background
		DrawRect(0, 0, SCREEN_WD, SCREEN_HT, GPACK_RGBA5551(0, 64, 0, 1));
		//Draw square
		DrawRect(square_pos_x, square_pos_y, SQUARE_SIZE, SQUARE_SIZE, GPACK_RGBA5551(255, 255, 255, 1));
		//Draw square borders
		DrawRect(square_pos_x, square_pos_y, SQUARE_SIZE, SQUARE_BORDER_W, GPACK_RGBA5551(0, 0, 0, 1));
		DrawRect(square_pos_x, square_pos_y, SQUARE_BORDER_W, SQUARE_SIZE, GPACK_RGBA5551(0, 0, 0, 1));
		DrawRect(square_pos_x+SQUARE_SIZE-SQUARE_BORDER_W, square_pos_y, SQUARE_BORDER_W, SQUARE_SIZE, GPACK_RGBA5551(0, 0, 0, 1));
		DrawRect(square_pos_x, square_pos_y+SQUARE_SIZE-SQUARE_BORDER_W, SQUARE_SIZE, SQUARE_BORDER_W, GPACK_RGBA5551(0, 0, 0, 1));
		//End frame
		gDPFullSync(glistp++);
		gSPEndDisplayList(glistp++);
		//Set task display list
		sc_task.list.t.data_ptr = (u64 *)glist;
		sc_task.list.t.data_size = (glistp-glist)*sizeof(Gfx); //In bytes
		//Set task framebuffer
		sc_task.framebuffer = cfb_table[cfb_idx];
		//Writeback whole data cache to let RCP see task properly
		osWritebackDCacheAll();
		//Send task to scheduler
		osSendMesg(osScGetCmdQ(&scheduler), (OSMesg)&sc_task, OS_MESG_BLOCK);
		//Wait for rendering to finish
		//Happens when done message is received
		do
		{
			osRecvMesg(&gfx_msg_queue, (OSMesg *)&wait_msg, OS_MESG_BLOCK);
		} while (wait_msg->type != OS_SC_DONE_MSG);
		//Swap framebuffer
		cfb_idx ^= 1;
	}
}