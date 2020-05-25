with Common_Type;  use Common_Type;
with Interfaces.C; use Interfaces.C;
with Ip;           use Ip;
with Net_Mem;      use Net_Mem;
with System;       use System;

package Tcp_Type is

   TCP_MAX_SYN_QUEUE_SIZE     : constant unsigned := 16;
   TCP_DEFAULT_SYN_QUEUE_SIZE : constant unsigned := 4;

   TCP_MAX_RX_BUFFER_SIZE : constant unsigned_long := 22_880;
   TCP_MAX_TX_BUFFER_SIZE : constant unsigned_long := 22_880;

   TCP_DEFAULT_MSS : constant unsigned_short := 536;
   TCP_MAX_MSS     : constant unsigned_short := 1_430;

   TCP_INITIAL_RTO : constant Systime := 1_000;

   TCP_INITIAL_WINDOW : constant unsigned_short := 3;

   -- Override timeout (should be in the range 0.1 to 1 seconds)
   TCP_OVERRIDE_TIMEOUT : constant Systime := 500;

   type Tcp_State is
     (TCP_STATE_CLOSED,
      TCP_STATE_LISTEN,
      TCP_STATE_SYN_SENT,
      TCP_STATE_SYN_RECEIVED,
      TCP_STATE_ESTABLISHED,
      TCP_STATE_CLOSE_WAIT,
      TCP_STATE_LAST_ACK,
      TCP_STATE_FIN_WAIT_1,
      TCP_STATE_FIN_WAIT_2,
      TCP_STATE_CLOSING,
      TCP_STATE_TIME_WAIT);
   
   for Tcp_State'Size use int'Size;

   for Tcp_State use 
      (TCP_STATE_CLOSED       => 0,
       TCP_STATE_LISTEN       => 1,
       TCP_STATE_SYN_SENT     => 2,
       TCP_STATE_SYN_RECEIVED => 3,
       TCP_STATE_ESTABLISHED  => 4,
       TCP_STATE_CLOSE_WAIT   => 5,
       TCP_STATE_LAST_ACK     => 6,
       TCP_STATE_FIN_WAIT_1   => 7,
       TCP_STATE_FIN_WAIT_2   => 8,
       TCP_STATE_CLOSING      => 9,
       TCP_STATE_TIME_WAIT    => 10
      );
   
   function Tcp_State_Convert (State : int) return Tcp_State is
      (if State = 0 then TCP_STATE_CLOSED
       elsif State = 1 then TCP_STATE_LISTEN
       elsif State = 2 then TCP_STATE_SYN_SENT
       elsif State = 3 then TCP_STATE_SYN_RECEIVED
       elsif State = 4 then TCP_STATE_ESTABLISHED
       elsif State = 5 then TCP_STATE_CLOSE_WAIT
       elsif State = 6 then TCP_STATE_LAST_ACK
       elsif State = 7 then TCP_STATE_FIN_WAIT_1
       elsif State = 8 then TCP_STATE_FIN_WAIT_2
       elsif State = 9 then TCP_STATE_CLOSING
       else TCP_STATE_TIME_WAIT);

   type TCP_Congest_State is new int;
      TCP_CONGEST_STATE_IDLE           : constant Tcp_Congest_State := 0;
      TCP_CONGEST_STATE_RECOVERY       : constant Tcp_Congest_State := 1;
      TCP_CONGEST_STATE_LOSS_RECOVERY  : constant Tcp_Congest_State := 2;

   subtype Tcp_Flags is uint8;

   TCP_FLAG_FIN : constant Tcp_Flags := 1;
   TCP_FLAG_SYN : constant Tcp_Flags := 2;
   TCP_FLAG_RST : constant Tcp_Flags := 4;
   TCP_FLAG_PSH : constant Tcp_Flags := 8;
   TCP_FLAG_ACK : constant Tcp_Flags := 16;
   TCP_FLAG_URG : constant Tcp_Flags := 32;

      -- TODO: use preprocessing instead of 14 to be coherent with the C code.
   type Chunk_Desc_Array is array (0 .. 14) of Chunk_Desc;

   type Tx_Buffer_Size is range 1 .. TCP_MAX_TX_BUFFER_SIZE;
   type Rx_Buffer_Size is range 1 .. TCP_MAX_RX_BUFFER_SIZE;

   type Tcp_Tx_Buffer is record
      chunkCount    : unsigned;
      maxChunkCound : unsigned;
      chunk         : Chunk_Desc_Array;
   end record
   with
      Convention => C;

   type Tcp_Rx_Buffer is record
      chunkCount    : unsigned;
      maxChunkCound : unsigned;
      chunk         : Chunk_Desc_Array;
   end record
   with
      Convention => C;

   type Tcp_Timer is record
      running   : Bool;
      startTime : Systime;
      interval  : Systime;
   end record
   with
      Convention => C;

   type Tcp_Queue_Item is record
      length : unsigned;
   end record
   with
      Convention => C;

   type Tcp_Sack_Block is record
      leftEdge  : unsigned_long;
      rightEdge : unsigned_long;
   end record
   with
      Convention => C;

   type SackBlockArray is array (0 .. 3) of Tcp_Sack_Block;

   type Tcp_Syn_Queue_Item;
   type Tcp_Syn_Queue_Item_Acc is access Tcp_Syn_Queue_Item;
   type Tcp_Syn_Queue_Item is
    record
      Next          : Tcp_Syn_Queue_Item_Acc;
      Net_Interface : System.Address;
      Src_Addr      : IpAddr;
      Src_Port      : Port;
      Dest_Addr     : IpAddr;
      Isn           : unsigned;
      Mss           : unsigned_short;
    end record
      with Convention => C;

end Tcp_Type;
