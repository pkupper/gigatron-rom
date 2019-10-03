/**********************************************************************************************/
/* gtemuAT67                                                                                  */
/*                                                                                            */ 
/* gtemuAT67 is an emulator for the Gigatron TTL microcomputer, written in C++ using SDL2.    */
/* This project provides Microsoft Windows support and should be compatible with Linux, MacOS */
/* and possibly even Android. As of this version it has only been tested on Windows 10 x64.   */
/**********************************************************************************************/


#include "memory.h"
#include "cpu.h"
#include "audio.h"
#include "editor.h"
#include "loader.h"
#include "timing.h"
#include "graphics.h"
#include "expression.h"
#include "assembler.h"
#include "compiler.h"


//#define COLLECT_INST_STATS
#if defined(COLLECT_INST_STATS)
    struct InstCount
    {
        uint8_t inst = 0;
        uint64_t count = 0;
    };

    uint64_t totalCount = 0;
    float totalPercent = 0.0f;
    std::vector<InstCount> instCounts(256);

    void displayInstCounts(void)
    {
        std::sort(instCounts.begin(), instCounts.end(), [](const InstCount& a, const InstCount& b)
        {
            return (a.count > b.count);
        });

        for(int i=0; i<instCounts.size(); i++)
        {
            float percent = float(instCounts[i].count)/float(totalCount)*100.0f;
            if(percent > 1.0f)
            {
                totalPercent += percent;
                fprintf(stderr, "inst:%02x count:%012lld %.1f%%\n", instCounts[i].inst, instCounts[i].count, percent);
            }
        }

        fprintf(stderr, "Total instructions:%lld\n", totalCount);
        fprintf(stderr, "Total percentage:%f\n", totalPercent);
    }
#endif


int main(int argc, char* argv[])
{
    Cpu::State S;

    Loader::initialise();
    Cpu::initialise(S);
    Memory::intitialise();
    Audio::initialise();
    Graphics::initialise();
    Editor::initialise();
    Expression::initialise();
    Assembler::initialise();
    Compiler::initialise();

    //Compiler::compile("gbas/test.gbas", "gbas/test.gasm");

    bool debugging = false;

    int vgaX = 0, vgaY = 0;
    int HSync = 0, VSync = 0;
    int64_t clock_prev = CLOCK_RESET;

    for(;;)
    {
        int64_t clock = Cpu::getClock();

        // MCP100 Power-On Reset
        if(clock < 0) S._PC = 0; 

        // Update CPU
        Cpu::State T = Cpu::cycle(S);

        HSync = (T._OUT & 0x40) - (S._OUT & 0x40);
        VSync = (T._OUT & 0x80) - (S._OUT & 0x80);
        
        // Falling vSync edge
        if(VSync < 0)
        {
            clock_prev = clock;
            vgaY = VSYNC_START;

            // Input and graphics
            if(!debugging)
            {
#if 1
                Editor::handleInput();
                Graphics::render();
#else
                Graphics::render();
                if(clock > 10000000) Graphics::tetris();
#endif
            }
        }

        // Pixel
        if(vgaX++ < HLINE_END)
        {
            if(vgaY >= 0  &&  vgaY < SCREEN_HEIGHT  &&  vgaX >=HPIXELS_START  &&  vgaX < HPIXELS_END)
            {
                Graphics::refreshPixel(S, vgaX-HPIXELS_START, vgaY, debugging);
            }
        }

        clock = Cpu::getClock();

#if defined(COLLECT_INST_STATS)
        totalCount++;
        instCounts[T._IR].count++;
        instCounts[T._IR].inst = T._IR;
        if(clock > STARTUP_DELAY_CLOCKS * 500.0)
        {
            displayInstCounts();
            _EXIT_(0);
        }
#endif        

        // RomType and Watchdog
        if(clock > STARTUP_DELAY_CLOCKS)
        {
            Cpu::setRomType();

            if(!debugging  &&  clock - clock_prev > CPU_STALL_CLOCKS)
            {
                clock_prev = CLOCK_RESET;
                Cpu::reset(true);
                vgaX = 0, vgaY = 0;
                HSync = 0, VSync = 0;
                fprintf(stderr, "main(): CPU stall for %" PRId64 " clocks : rebooting.\n", clock - clock_prev);
            }
        }

        // Rising hSync edge
        if(HSync > 0)
        {
            Cpu::setXOUT(T._AC);
            
            // Audio
            if(Audio::getRealTimeAudio())
            {
                Audio::playSample();
            }
            else
            {
                Audio::fillAudioBuffer();
                if(vgaY == SCREEN_HEIGHT+4) Audio::playAudioBuffer();
            }

            // Loader
            Loader::upload(vgaY);

            // Horizontal timing errors
            if(vgaY >= 0  &&  vgaY < SCREEN_HEIGHT)
            {
                static uint32_t colour = 0xFF220000;
                if((vgaY % 4) == 0) colour = 0xFF220000;
                if(vgaX != 200)
                {
                    colour = 0xFFFF0000;
                    //fprintf(stderr, "main(): Horizontal timing error : vgaX %03d : vgaY %03d : xout %02x : time %0.3f\n", vgaX, vgaY, T._AC, float(clock)/6.250e+06f);
                }
                if((vgaY % 4) == 3) Graphics::refreshTimingPixel(S, 160, (vgaY/4) % GIGA_HEIGHT, colour, debugging);
            }

            vgaX = 0;
            vgaY++;

            // Change this once in a while
            T._undef = rand() & 0xff;
        }

        // Debugger
        debugging = Editor::singleStepDebug();

        // vCPU instruction slot utilisation
        Cpu::vCpuUsage(S);
        
#if 0
        Audio::playMusic();
#endif

        // Master clock
        clock = Cpu::getClock();
        Cpu::setClock(++clock);

        S=T;
    }

    return 0;
}