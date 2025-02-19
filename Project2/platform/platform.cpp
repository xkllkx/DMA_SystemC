/*
 *
 * Copyright (c) 2005-2021 Imperas Software Ltd., www.imperas.com
 *
 * The contents of this file are provided under the Software License
 * Agreement that you accepted before downloading this file.
 *
 * This source forms part of the Software and can be used for educational,
 * training, and demonstration purposes but cannot be used for derivative
 * works except in cases where the derivative works require OVP technology
 * to run.
 *
 * For open source models released under licenses that you can use for
 * derivative works, please visit www.OVPworld.org or www.imperas.com
 * for the location of the open source models.
 *
 */

#include "systemc.h"
#include "tlm/tlmModule.hpp"
#include "tlm/tlmDecoder.hpp"
#include "tlm/tlmMemory.hpp"

// Processor configuration
#ifdef RISCV32
#include "riscv.ovpworld.org/processor/riscv/1.0/tlm/riscv_RV32I.igen.hpp"
#endif
#ifdef ARM7TDMI
#include "arm.ovpworld.org/processor/arm/1.0/tlm/arm_ARM7TDMI.igen.hpp"
#endif
#ifdef IMG_MIPS32R2
#include "mips.ovpworld.org/processor/mips32_r1r5/1.0/tlm/mips32_r1r5_4KEm.igen.hpp"
#endif

using namespace sc_core;

#define DMA_BASE                  0x100000
#define DMA_SOURCE_ADDR1          0x00
#define DMA_TARGET_ADDR1          0x04
#define DMA_SIZE_ADDR1            0x08
#define DMA_CLEAR_ADDR1           0x0c
#define DMA_SOURCE_ADDR2          0x10
#define DMA_TARGET_ADDR2          0x14
#define DMA_SIZE_ADDR2            0x18
#define DMA_CLEAR_ADDR2           0x1c

////////////////////////////////////////////////////////////////////////////////
//                      BareMetal Class                                       //
////////////////////////////////////////////////////////////////////////////////


SC_MODULE(DMA)
{
    //Ports.
    sc_in<bool> clk, reset;
    tlm::tlm_analysis_port<unsigned int> interrupt1;
    tlm::tlm_analysis_port<unsigned int> interrupt2;

    //Data members.
    //RISCV-1
    sc_signal<sc_uint<32> > SOURCE1, TARGET1, SIZE1;
    sc_signal<bool> START_CLEAR1;
    sc_signal<sc_uint<32> > offset1;
    //RISCV-2
    sc_signal<sc_uint<32> > SOURCE2, TARGET2, SIZE2;
    sc_signal<bool> START_CLEAR2;
    sc_signal<sc_uint<32> > offset2;
    //The first socket template argument specifies the typename of the parent module.
    tlm_utils::simple_initiator_socket<DMA> master_p;
    tlm_utils::simple_target_socket<DMA> slave_p;

    //Function members.
    void b_transport(tlm::tlm_generic_payload& trans, sc_time& delay);
    void dma_p();
    
    //buffer
    unsigned int data;
    bool stop1, stop2;
    //address
    unsigned int reg_addr1[4];
    unsigned int reg_addr2[4];

    //Constructor.
    DMA(sc_module_name _name): sc_module(_name), master_p("master_p"), slave_p("slave_p")
    {
        SC_HAS_PROCESS(DMA);
        
        // Register callback for incoming b_transport interface method call
        slave_p.register_b_transport(this, &DMA::b_transport);
        
        SC_CTHREAD(dma_p, clk.pos());
        async_reset_signal_is(reset, false);
    }
};


class BareMetal : public sc_module {

  public:
    BareMetal (sc_module_name name);

    tlmModule             Platform;
    tlmDecoder            busM, bus1, bus2;
    tlmRam                ram1, ram2, ram3, ram4, ram1_1, ram2_1;
#ifdef RISCV32
    riscv_RV32I           cpu1, cpu2;
#endif
#ifdef ARM7TDMI
    arm_ARM7TDMI          cpu1;
#endif
#ifdef IMG_MIPS32R2
    mips32_r1r5_4KEm      cpu1;
#endif 
    DMA                   dma;
  private:

    params paramsForcpu1() {
        params p;
        p.set("defaultsemihost", true);
        return p;
    }
    
    params paramsForcpu2() {
        params p;
        p.set("defaultsemihost", true);
        return p;
    }

}; /* BareMetal */

BareMetal::BareMetal (sc_module_name name)
    : sc_module (name)
    , Platform ("")
    , busM (Platform, "busM", 3, 3) //(linked master ports, linked slave ports)
    , bus1 (Platform, "bus1", 2, 3) //(linked master ports, linked slave ports)
    , bus2 (Platform, "bus2", 2, 3) //(linked master ports, linked slave ports)
    , ram1 (Platform, "ram1", 0x100000)
    , ram2 (Platform, "ram2", 0x100000)
    , ram3 (Platform, "ram3", 0x100000)
    , ram4 (Platform, "ram4", 0x100000)
    , ram1_1 (Platform, "ram1_1", 0x100000)
    , ram2_1 (Platform, "ram2_1", 0x100000)
    , cpu1 (Platform, "cpu1",  paramsForcpu1())
    , cpu2 (Platform, "cpu2",  paramsForcpu2())
    , dma("dma")
{
    bus1.connect(cpu1.INSTRUCTION);
    bus1.connect(cpu1.DATA);
    bus2.connect(cpu2.INSTRUCTION);
    bus2.connect(cpu2.DATA);
    bus1.connect(busM);
    bus2.connect(busM);
    bus1.connect(ram1.sp1, 0x000000, 0x0fffff); //(start address, end address)
    bus2.connect(ram2.sp1, 0x000000, 0x0fffff); //(start address, end address)
    busM.connect(ram3.sp1, 0x200000, 0x2fffff); //(start address, end address)
    busM.connect(ram4.sp1, 0x300000, 0x3fffff); //(start address, end address)
    bus1.connect(ram1_1.sp1, 0xfff00000, 0xffffffff);   //Additional stack issues.
    bus2.connect(ram2_1.sp1, 0xfff00000, 0xffffffff);   //Additional stack issues.
    //-------------------------------------
    //-----slave port (DMA)-----
    busM.nextInitiatorSocket(0x100000, 0x10001f)->bind(dma.slave_p);
    //-----master port (DMA)------
    dma.master_p.bind(*busM.nextTargetSocket());
    //-------------------------------------
    dma.interrupt1(cpu1.MExternalInterrupt);
    dma.interrupt2(cpu2.MExternalInterrupt);
    //-------------------------------------
}

void DMA::dma_p()
{
    SOURCE1 = 0x0; TARGET1 = 0x0; SIZE1 = 0x0; START_CLEAR1 = false; offset1 = 0;
    SOURCE2 = 0x0; TARGET2 = 0x0; SIZE2 = 0x0; START_CLEAR2 = false; offset2 = 0;
    interrupt1.write(0);
    interrupt2.write(0);
    for(int j =0; j<4; j++)
    {
        reg_addr1[j] = 0;
        reg_addr2[j] = 0;
    }
    stop1 = false; stop2 = false;
    sc_time delay = sc_time(10, SC_NS);
    unsigned int addr_s, addr_t, data_s, data_t;

    tlm::tlm_generic_payload* trans = new tlm::tlm_generic_payload;

    while(1)
    {
        wait();
        
        stop1 = !(SIZE1.read()-offset1.read() > 0);
        stop2 = !(SIZE2.read()-offset2.read() > 0);
        
        //Transmit to RAM1
        if(!START_CLEAR1.read()) //The SOURCE, TARGET, SIZE info is not recieved yet.
        {
            //Recieve SOURCE, TARGET, SIZE info from CPU.
            SOURCE1 = reg_addr1[0];
            TARGET1 = reg_addr1[1];
            SIZE1 = reg_addr1[2];
            START_CLEAR1 = reg_addr1[3];
            
            interrupt1.write(0);
            offset1 = 0;
        }
        //The SOURCE, TARGET, SIZE info is recieved and START_CLEAR is on.
        else if(!stop1)
        {
            addr_s = SOURCE1.read();
            addr_t = TARGET1.read();

            //Read data from source address.
            cout << "\nRead from source: " << endl;
            tlm::tlm_command cmd = static_cast<tlm::tlm_command>(0);    //Read.
            trans->set_command( cmd );
            trans->set_address( addr_s+offset1.read() );
            trans->set_data_ptr( reinterpret_cast<unsigned char*>(&data_s) );
            trans->set_data_length( 4 );
            trans->set_streaming_width( 4 ); // = data_length to indicate no streaming
            trans->set_byte_enable_ptr( 0 ); // 0 indicates unused
            trans->set_dmi_allowed( false ); // Mandatory initial value
            trans->set_response_status( tlm::TLM_INCOMPLETE_RESPONSE ); // Mandatory initial value
            master_p->b_transport( *trans, delay );  // Blocking transport call

            cout << "RAM1: trans = { " << (cmd ? 'W' : 'R') << ", " << hex << addr_s+offset1.read()
                 << " } , data_s = " << hex << data_s << " at time " << sc_time_stamp()
                 << " delay = " << delay << endl;
         
            data_t = data_s;    //Data transfer.
            
            //Write data to source address.
            cout << "Write to target: " << endl;
            cmd = static_cast<tlm::tlm_command>(1);    //Write.
            trans->set_command( cmd );
            trans->set_address( addr_t+offset1.read() );
            trans->set_data_ptr( reinterpret_cast<unsigned char*>(&data_t) );
            trans->set_data_length( 4 );
            trans->set_streaming_width( 4 ); // = data_length to indicate no streaming
            trans->set_byte_enable_ptr( 0 ); // 0 indicates unused
            trans->set_dmi_allowed( false ); // Mandatory initial value
            trans->set_response_status( tlm::TLM_INCOMPLETE_RESPONSE ); // Mandatory initial value
            master_p->b_transport( *trans, delay );  // Blocking transport call

            cout << "RAM1: trans = { " << (cmd ? 'W' : 'R') << ", " << hex << addr_t+offset1.read()
                 << " } , data_t = " << hex << data_t << " at time " << sc_time_stamp()
                 << " delay = " << delay << endl;
            
            offset1 = offset1.read() + 4;
            
        }
        else if(stop1)
        {
            //cout << "Finish 1" << endl;
            interrupt1.write(1);
            START_CLEAR1 = reg_addr1[3];
        }
        
        //Transmit to RAM2
        if(!START_CLEAR2.read()) //The SOURCE, TARGET, SIZE info is not recieved yet.
        {
            //Recieve SOURCE, TARGET, SIZE info from CPU.
            SOURCE2 = reg_addr2[0];
            TARGET2 = reg_addr2[1];
            SIZE2 = reg_addr2[2];
            START_CLEAR2 = reg_addr2[3];
            
            interrupt2.write(0);
            offset2 = 0;
        }
        //The SOURCE, TARGET, SIZE info is recieved and START_CLEAR is on.
        else if(!stop2)
        {
            addr_s = SOURCE2.read();
            addr_t = TARGET2.read();

            //Read data from source address.
            cout << "\nRead from source: " << endl;
            tlm::tlm_command cmd = static_cast<tlm::tlm_command>(0);    //Read.
            trans->set_command( cmd );
            trans->set_address( addr_s+offset2.read() );
            trans->set_data_ptr( reinterpret_cast<unsigned char*>(&data_s) );
            trans->set_data_length( 4 );
            trans->set_streaming_width( 4 ); // = data_length to indicate no streaming
            trans->set_byte_enable_ptr( 0 ); // 0 indicates unused
            trans->set_dmi_allowed( false ); // Mandatory initial value
            trans->set_response_status( tlm::TLM_INCOMPLETE_RESPONSE ); // Mandatory initial value
            master_p->b_transport( *trans, delay );  // Blocking transport call

            cout << "trans = { " << (cmd ? 'W' : 'R') << ", " << hex << addr_s+offset2.read()
                 << " } , data_s = " << hex << data_s << " at time " << sc_time_stamp()
                 << " delay = " << delay << endl;
         
            data_t = data_s;    //Data transfer.
            
            //Write data to source address.
            cout << "Write to target: " << endl;
            cmd = static_cast<tlm::tlm_command>(1);    //Write.
            trans->set_command( cmd );
            trans->set_address( addr_t+offset2.read() );
            trans->set_data_ptr( reinterpret_cast<unsigned char*>(&data_t) );
            trans->set_data_length( 4 );
            trans->set_streaming_width( 4 ); // = data_length to indicate no streaming
            trans->set_byte_enable_ptr( 0 ); // 0 indicates unused
            trans->set_dmi_allowed( false ); // Mandatory initial value
            trans->set_response_status( tlm::TLM_INCOMPLETE_RESPONSE ); // Mandatory initial value
            master_p->b_transport( *trans, delay );  // Blocking transport call

            cout << "trans = { " << (cmd ? 'W' : 'R') << ", " << hex << addr_t+offset2.read()
                 << " } , data_t = " << hex << data_t << " at time " << sc_time_stamp()
                 << " delay = " << delay << endl;
            
            offset2 = offset2.read() + 4;
            
            stop2 = !(SIZE2.read()-offset2.read() > 0);
        }
        else if(stop2)
        {
            //cout << "Finish 2" << endl;
            interrupt2.write(1);
            START_CLEAR2 = reg_addr2[3];
        }
    }

    return;
}

void DMA::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
    // TLM-2 blocking transport method

    tlm::tlm_command cmd = trans.get_command();
    sc_dt::uint64    adr = trans.get_address();
    unsigned char*   ptr = trans.get_data_ptr();
    //cout << "ADR=" << hex << adr << ", PTR=" << hex << (Uns32)(*ptr) << endl;
    if(cmd == tlm::TLM_WRITE_COMMAND)
    {
        //RISCV-1
        if((stop1 || !START_CLEAR1.read()) &&   //Avoid refreshing data registers when moving data.
           (adr == /*DMA_BASE+*/DMA_SOURCE_ADDR1 ||
            adr == /*DMA_BASE+*/DMA_TARGET_ADDR1 ||
            adr == /*DMA_BASE+*/DMA_SIZE_ADDR1 ||
            adr == /*DMA_BASE+*/DMA_CLEAR_ADDR1))
        {
            memcpy(reg_addr1+(adr/*-DMA_BASE*/-DMA_SOURCE_ADDR1)/4, ptr, 4);
            cout << "Recieved RISCV-1's registers:";
            if(adr == /*DMA_BASE+*/DMA_SOURCE_ADDR1)
                cout << "\tSOURCE1:\t" << hex << reg_addr1[0] << endl;
            else if(adr == /*DMA_BASE+*/DMA_TARGET_ADDR1)
                cout << "\tTARGET1:\t" << hex << reg_addr1[1] << endl;
            else if(adr == /*DMA_BASE+*/DMA_SIZE_ADDR1)
                cout << "\tSIZE1:\t\t" << hex << reg_addr1[2] << endl;
            else if(adr == /*DMA_BASE+*/DMA_CLEAR_ADDR1)
                cout << "\tSTART/CLEAN1:\t" << hex << reg_addr1[3] << endl;
        }
        //RISCV-2
        else if((stop2 || !START_CLEAR2.read()) &&  //Avoid refreshing data registers when moving data.
                (adr == /*DMA_BASE+*/DMA_SOURCE_ADDR2 ||
                 adr == /*DMA_BASE+*/DMA_TARGET_ADDR2 ||
                 adr == /*DMA_BASE+*/DMA_SIZE_ADDR2 ||
                 adr == /*DMA_BASE+*/DMA_CLEAR_ADDR2))
        {
            memcpy(reg_addr2+(adr/*-DMA_BASE*/-DMA_SOURCE_ADDR2)/4, ptr, 4);
            cout << "Recieved RISCV-2's registers:";
            if(adr == /*DMA_BASE+*/DMA_SOURCE_ADDR2)
                cout << "\tSOURCE2:\t" << hex << reg_addr2[0] << endl;
            else if(adr == /*DMA_BASE+*/DMA_TARGET_ADDR2)
                cout << "\tTARGET2:\t" << hex << reg_addr2[1] << endl;
            else if(adr == /*DMA_BASE+*/DMA_SIZE_ADDR2)
                cout << "\tSIZE2:\t\t" << hex << reg_addr2[2] << endl;
            else if(adr == /*DMA_BASE+*/DMA_CLEAR_ADDR2)
                cout << "\tSTART/CLEAN2:\t" << hex << reg_addr2[3] << endl;
        }

        wait(delay);
    }

    // Obliged to set response status to indicate successful completion
    trans.set_response_status( tlm::TLM_OK_RESPONSE );

    return;
}

int sc_main (int argc, char *argv[]) {

    // start the CpuManager session
    session s;

    // create a standard command parser and parse the command line
    parser  p(argc, (const char**) argv);

    // create an instance of the platform
    BareMetal top ("top");
    
	sc_clock clk("clk", 500, SC_NS);
	sc_signal<bool> rst;
    
	top.dma.reset(rst);
	top.dma.clk(clk);
    
	rst.write(true);

    // start SystemC
    sc_start();
    
    return 0;
}

