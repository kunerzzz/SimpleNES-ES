#include "Emulator.h"
#include "Log.h"
#include "FrameBuffer.h"

#include <thread>
#include <chrono>

namespace sn
{
    Emulator::Emulator() :
        m_cpu(m_bus),
        m_ppu(m_pictureBus, m_emulatorScreen),
        m_cycleTimer(),
        m_cpuCycleDuration(std::chrono::nanoseconds(559))
    {
        if(!m_bus.setReadCallback(PPUSTATUS, [&](void) {return m_ppu.getStatus();}) ||
            !m_bus.setReadCallback(PPUDATA, [&](void) {return m_ppu.getData();}) ||
            !m_bus.setReadCallback(JOY1, [&](void) {return m_controller1.read();}) ||
            !m_bus.setReadCallback(JOY2, [&](void) {return m_controller2.read();}) ||
            !m_bus.setReadCallback(OAMDATA, [&](void) {return m_ppu.getOAMData();}))
        {
            LOG(Error) << "Critical error: Failed to set I/O callbacks" << std::endl;
        }


        if(!m_bus.setWriteCallback(PPUCTRL, [&](Byte b) {m_ppu.control(b);}) ||
            !m_bus.setWriteCallback(PPUMASK, [&](Byte b) {m_ppu.setMask(b);}) ||
            !m_bus.setWriteCallback(OAMADDR, [&](Byte b) {m_ppu.setOAMAddress(b);}) ||
            !m_bus.setWriteCallback(PPUADDR, [&](Byte b) {m_ppu.setDataAddress(b);}) ||
            !m_bus.setWriteCallback(PPUSCROL, [&](Byte b) {m_ppu.setScroll(b);}) ||
            !m_bus.setWriteCallback(PPUDATA, [&](Byte b) {m_ppu.setData(b);}) ||
            !m_bus.setWriteCallback(OAMDMA, [&](Byte b) {DMA(b);}) ||
            !m_bus.setWriteCallback(JOY1, [&](Byte b) {m_controller1.strobe(b); m_controller2.strobe(b);}) ||
            !m_bus.setWriteCallback(OAMDATA, [&](Byte b) {m_ppu.setOAMData(b);}))
        {
            LOG(Error) << "Critical error: Failed to set I/O callbacks" << std::endl;
        }

        m_ppu.setInterruptCallback([&](){ m_cpu.interrupt(CPU::NMI); });
    }

    void Emulator::run(std::string rom_path)
    {
        if (!m_cartridge.loadFromFile(rom_path))
            return;

        m_mapper = Mapper::createMapper(static_cast<Mapper::Type>(m_cartridge.getMapper()),
                                        m_cartridge,
                                        [&](){ m_pictureBus.updateMirroring(); });
        if (!m_mapper)
        {
            LOG(Error) << "Creating Mapper failed. Probably unsupported." << std::endl;
            return;
        }

        if (!m_bus.setMapper(m_mapper.get()) ||
            !m_pictureBus.setMapper(m_mapper.get()))
            return;

        m_cpu.reset();
        m_ppu.reset();

        // m_window.create(sf::VideoMode(NESVideoWidth * m_screenScale, NESVideoHeight * m_screenScale),
        //                 "SimpleNES", sf::Style::Titlebar | sf::Style::Close);
        // m_window.setVerticalSyncEnabled(true);

        m_framebuffer.create(NESVideoWidth, NESVideoHeight);
        m_emulatorScreen.create(NESVideoWidth, NESVideoHeight, 0xffffff);

        const char *input_dev_p1 = getenv("NES_CONTROLLER1");
        const char *input_dev_p2 = getenv("NES_CONTROLLER2");

        if(input_dev_p1 && strlen(input_dev_p1)) {
            LOG(Info) << "Start controller 1 (dev path: " << input_dev_p1 << ")" << std::endl;
            m_controller1.create(input_dev_p1);
        }
        if(input_dev_p2 && strlen(input_dev_p2)) {
            LOG(Info) << "Start controller 2 (dev path: " << input_dev_p2 << ")" << std::endl;
            m_controller2.create(input_dev_p2);
        }

        m_cycleTimer = std::chrono::high_resolution_clock::now();
        m_elapsedTime = m_cycleTimer - m_cycleTimer;

        bool focus = true, pause = false;
        // sf::Event event;
        // while (m_window.isOpen())
        // {
        //     while (m_window.pollEvent(event))
        //     {
        //         if (event.type == sf::Event::Closed ||
        //         (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape))
        //         {
        //             m_window.close();
        //             return;
        //         }
        //         else if (event.type == sf::Event::GainedFocus)
        //         {
        //             focus = true;
        //             m_cycleTimer = std::chrono::high_resolution_clock::now();
        //         }
        //         else if (event.type == sf::Event::LostFocus)
        //             focus = false;
        //         else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F2)
        //         {
        //             pause = !pause;
        //             if (!pause)
        //                 m_cycleTimer = std::chrono::high_resolution_clock::now();
        //         }
        //         else if (pause && event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::F3)
        //         {
        //             for (int i = 0; i < 29781; ++i) //Around one frame
        //             {
        //                 //PPU
        //                 m_ppu.step();
        //                 m_ppu.step();
        //                 m_ppu.step();
        //                 //CPU
        //                 m_cpu.step();
        //             }
        //         }
        //         else if (focus && event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::F4)
        //         {
        //             Log::get().setLevel(Info);
        //         }
        //         else if (focus && event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::F5)
        //         {
        //             Log::get().setLevel(InfoVerbose);
        //         }
        //         else if (focus && event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::F6)
        //         {
        //             Log::get().setLevel(CpuTrace);
        //         }
        //     }
        while(1) {
            if (focus && !pause)
            {
                m_elapsedTime += std::chrono::high_resolution_clock::now() - m_cycleTimer;
                m_cycleTimer = std::chrono::high_resolution_clock::now();

                while (m_elapsedTime > m_cpuCycleDuration)
                {
                    //PPU
                    m_ppu.step();
                    m_ppu.step();
                    m_ppu.step();
                    //CPU
                    m_cpu.step();

                    m_elapsedTime -= m_cpuCycleDuration;
                }

                m_framebuffer.draw(m_emulatorScreen);
                m_framebuffer.display();
            }
            else
            {
                // sf::sleep(sf::milliseconds(1000/60));
                std::this_thread::sleep_for(std::chrono::milliseconds(1000/60)); //1/60 second
            }
        }
    }

    void Emulator::DMA(Byte page)
    {
        m_cpu.skipDMACycles();
        auto page_ptr = m_bus.getPagePtr(page);
        m_ppu.doDMA(page_ptr);
    }

    void Emulator::setVideoHeight(int height)
    {
        // m_screenScale = height / float(NESVideoHeight);
        // LOG(Info) << "Scale: " << m_screenScale << " set. Screen: "
        //           << int(NESVideoWidth * m_screenScale) << "x" << int(NESVideoHeight * m_screenScale) << std::endl;
    }

    void Emulator::setVideoWidth(int width)
    {
        // m_screenScale = width / float(NESVideoWidth);
        // LOG(Info) << "Scale: " << m_screenScale << " set. Screen: "
        //           << int(NESVideoWidth * m_screenScale) << "x" << int(NESVideoHeight * m_screenScale) << std::endl;

    }
    void Emulator::setVideoScale(float scale)
    {
        // m_screenScale = scale;
        // LOG(Info) << "Scale: " << m_screenScale << " set. Screen: "
        //           << int(NESVideoWidth * m_screenScale) << "x" << int(NESVideoHeight * m_screenScale) << std::endl;
    }

    void Emulator::setKeys(std::vector<uint16_t>& p1, std::vector<uint16_t>& p2)
    {
        m_controller1.setKeyBindings(p1);
        m_controller2.setKeyBindings(p2);
    }

    void Emulator::cleanup()
    {
        m_controller1.~Controller();
        m_controller2.~Controller();
        m_framebuffer.~FrameBuffer();
    }

}