#include "picosystem.hpp"

#include "pico/multicore.h"
#include "hardware/structs/bus_ctrl.h" //so we can set high bus priority on Core1 & have contention counters
#include <cstring> //memcpy etc.
#include <math.h>

using namespace picosystem;

//Comment out/in defines if needed (for debugging)
//#define GAMESCOM //enables tweaks for a gamescom version with peace loving balloons instead of zombies
//#define SKIP_START //skips the starting splash/menu and goes straight into normal gameplay (menu = 0)
//#define FREE_ROAM //set to ignore chunk physics for player
//#define DEBUG_SHADERS //debug shaders are those with shader_id >= 250

//Defines for performance profiling
//#define DEBUG_INFO //adds information on core times and triangle counts in the main menu
//#define NO_NPCS //disable all npcs including Zombies
//#define RASTERIZER_IN_FLASH //puts the render_rasterize function for core 1 into flash instead of scratch RAM
//#define FRAME_COUNTER //tallies frametimes for performance analysis
//#define CONTENTION_COUNTER //enables counters for RAM contention on the 4 main banks
//#define CPU_LED_LOAD //CPU load on LED (Core1: Green-40fps, Yellow-20fps, Red-10fps), blue if core 0 overloaded (logic too slow)

int32_t logic_time;

int32_t show_battery = 0;

#include "engine/render_globals.h"
#include "engine/chunk_globals.h" //chunk settings
#include "chunk_data.h" //contains all the chunk data of the game world (exported by Blender addon)
#include "game/logic_globals.h"

//Test files
#ifdef DEBUG_SHADERS
#include "test_models/test_texture.h"
#endif
//Models (see render_model_16bit function)
//#include "test_models/plane.h"
//#include "test_models/cube.h"
//#include "test_models/suzanne.h"


//Rendering & chunk system code
#include "engine/render_math.h"
#include "engine/render_clipping.cpp"
#include "engine/render_culling.cpp"
#include "engine/render_lighting.cpp"
#include "engine/render_triangle.cpp"
#include "engine/render_camera.cpp"
#include "engine/render_sync.cpp"
#include "engine/render_rasterize.cpp"
#include "engine/render_model.cpp"
#include "engine/render_chunk.cpp"


//logic code for the example game is decoupled as much as possible in a separate folder
#include "game/logic_day_night_cycle.cpp"
#include "game/logic_info_text.cpp"
#include "game/logic_menu.cpp"

//Objects
#include "game/grass.h"
#include "game/logic_grass.cpp"
#include "game/gate.h"
#include "game/logic_gate.cpp"

//NPCs
#include "game/npc.h"
#include "game/npcleft.h"
#include "game/npcright.h"
#include "game/logic_npc.cpp"

#include "game/logic_quest_npcs.cpp"

#ifdef GAMESCOM
//Balloons for gamescom
#include "game/gamescom/balloon.h"
#include "game/gamescom/balloon_pop.h"
#include "game/gamescom/logic_balloon.cpp"
#include "game/gamescom/logic_shoot_balloon.cpp"

#else

//Zombies
#include "game/zombie_fast_stand.h"
#include "game/zombie_fast_left.h"
#include "game/zombie_fast_right.h"
#include "game/zombie_slouch.h"
#include "game/zombie_dead.h"
#include "game/zombie_attack.h"
#include "game/logic_zombies.cpp"
#include "game/logic_shoot.cpp"

#endif

//event handling
#include "game/logic_demo.cpp"
#include "game/logic_events.cpp"

//input handling
#include "game/logic_input.cpp"






//set core 1 on its dedicated rasterization function
void core1_entry() {
    while (1) {
        render_rasterize();
    }
}




void init() {
    //Launch the rasterizer on core 1
    multicore_launch_core1(core1_entry);
    
    //set core1 to highest bus priority
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    #ifdef CONTENTION_COUNTER
    //performance counters / contention for SRAM
    bus_ctrl_hw->counter[0].sel = arbiter_sram0_perf_event_access_contested;
    bus_ctrl_hw->counter[1].sel = arbiter_sram1_perf_event_access_contested;
    bus_ctrl_hw->counter[2].sel = arbiter_sram2_perf_event_access_contested;
    bus_ctrl_hw->counter[3].sel = arbiter_sram3_perf_event_access_contested;
    #endif


    //We first need to tell core 1 to start rasterizing the first empty dummy frame
    multicore_fifo_push_blocking(0);
    

    //start the game immediately if desired, skipping start screen
    #ifdef SKIP_START
        menu = 0;
        logic_new_game();
    #endif

    //set display to max brightness for the gamescom version
    #ifdef GAMESCOM
        backlight(100);
    #endif


}


void update(uint32_t tick) {

    //update the global time
    global_time = time();

    //logic time for profiling (only update if no frames are skipped)
    #ifdef DEBUG_INFO
    if (skip_frame == 0) {
        logic_time = time_us();
    }
    #endif

    render_quest_npcs();

    //we prepare the triangles for the scenery/chunks here since the draw() function is already
    //heavily loaded with npc calculations
    render_chunks();



    //process input
    logic_input();

    //process events/move camera in demo mode
    logic_events();

    //process daylight
    logic_day_night_cycle();

    //get current area of player
    logic_player_area();

    //process zombies
    logic_zombies();

    //process npcs
    logic_npc();

    //calculate grass swaying motion (offset)
    logic_grass();



    //increase value for animated textures
    animated_texture_counter++;
    if (animated_texture_counter == 64) {
        animated_texture_counter = 0;
    }
    
    animated_texture_offset = animated_texture_counter / 2;


    //we prepare the matrix to convert objects from world to view/projection space
    float mat_vp_float[4][4];
    mat_mul(mat_projection, mat_camera, mat_vp_float);

    //convert the resulting matrix to integers
    mat_convert_float_fixed(mat_vp_float, mat_vp);

    #ifdef DEBUG_INFO
    if (skip_frame == 0) {
        logic_time = time_us() - logic_time;
    }
    #endif
}


void draw(uint32_t tick) {


    //Measuring performance is important, hence lots of debug to check how long something takes (start timer on core0)
    uint32_t core0_time = time_us();
    uint32_t core1_time;

    //Sync up with core1 here (swap Framebuffers and triangle lists etc.)
    core1_time = render_sync();


    //if core 1 has not managed to perform its task within the allocated time, skip any further processing and go to next frame
    if (core1_time == 0) {
        skip_frame = 1;
        return;
    //note we also set a skip_frame variable to let code in the update function know if it is safe to add triangles
    } else {
        skip_frame = 0;
    }
    
    #ifdef DEBUG_INFO
    rendered_triangles = number_triangles; //for debug purposes
    #endif

    //initialize the triangle counter so we know how many triangles are to be rendered by Core1 (be sure not to exceed MAX_TRIANGLES)
    number_triangles = 0;


    //we now fill the triangle list which is to be rendered by Core1 in the next frame

    //here is a simple function to load a single model at world origin (next to the left gate when going to the outskirts)
    //render_model_16bit(testmodel, TESTMODEL);
    //The Suzanne model is too memory heavy and is streamed in from flash memory at the cost of performance
    //render_model_16bit_flash(testmodel, TESTMODEL);


    //render objects close to the camera to reduce overdraw
    //foliage
    render_grass();

    //load objects
    render_gate();


    //load stationary npcs (shops/quest givers) if close to the player



    //render zombies. Note we prioritize the zombies before NPCs in the city, as they share the same triangle budget.
    //Having them disappear in front of a player when running out of tris might be the difference between life & death ;)
    render_zombies();

    //load triangle list with other moving npcs
    render_npcs();
    


    //note that we add triangles for the world/scenery chunks in the update() function as we simply would run out of time here


    //display UI unless a menu is open
    if (menu == 0) {
        display_info();

    //display a game menu
    } else {
        display_menu();
    }





    //DEBUG
    //clear the screen when all else fails to get at least debug output
    /*
    pen(0, 0, 0);
    clear();
    */


    #ifdef DEBUG_INFO
    if (menu == MENU_MAIN) {
        pen(15, 15, 15);

        //flipping time
        text("PIO:" + str(stats.flip_us), 60, 80);

        //Core1 rasterization time/skipped frames stats
        //text("Skipped Frames: " + str(skipped_frames), 0, 100);
        text("C1: " + str(core1_time), 60, 90);

        core0_time = time_us() - core0_time;
        //Finally we get the time for Core0 draw routine which should not exceed 12ms/12000us
        text("C0D: " + str(core0_time), 0, 90);
    }
    #endif

    #ifdef CONTENTION_COUNTER
    uint32_t sram1_contested = bus_ctrl_hw->counter[0].value;
    uint32_t sram2_contested = bus_ctrl_hw->counter[1].value;
    uint32_t sram3_contested = bus_ctrl_hw->counter[2].value;
    uint32_t sram4_contested = bus_ctrl_hw->counter[3].value;

    text("R1:" + str(sram1_contested), 0, 0);
    text("R2:" + str(sram2_contested), 0, 10);
    text("R3:" + str(sram3_contested), 0, 20);
    text("R4:" + str(sram4_contested), 0, 30);
    #endif

    //if Core0 struggles with workload causing logic and fps drop, show blue on the LED
    #ifdef CPU_LED_LOAD
    if (stats.fps < 40) {
        led(0, 0, 25);
    }
    #endif

    //low battery level once we reach 30%
    #ifdef GAMESCOM
        if (battery() < 30) {
            led(25, 0, 0);
        }
    #endif

}

