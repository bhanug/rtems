/*
 *  This file contains the termios TTY driver for the UART found
 *  on the Synova Mongoose-V.
 *
 *  COPYRIGHT (c) 1989-2001.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id$
 */

#include <rtems.h>
#include <rtems/libio.h>
#include <stdlib.h>

#include <libchip/serial.h>
#include <libchip/mg5uart.h>
#include <libchip/sersupp.h>
#include <libcpu/mongoose-v.h>

extern void set_vector( rtems_isr_entry, rtems_vector_number, int );

/*
 *  Indices of registers
 */

/*
 *  Per chip context control
 */

typedef struct _mg5uart_context
{
  int            mate;
} mg5uart_context;

/*
 *  Define MG5UART_STATIC to nothing while debugging so the entry points
 *  will show up in the symbol table.
 */

#define MG5UART_STATIC

/* #define MG5UART_STATIC static */

#define MG5UART_SETREG( _base, _register, _value ) \
        MONGOOSEV_WRITE_REGISTER( _base, _register, _value )

#define MG5UART_GETREG( _base, _register ) \
        MONGOOSEV_READ_REGISTER( _base, _register )

/*
 *  Console Device Driver Support Functions
 */

MG5UART_STATIC int mg5uart_baud_rate(
  int           minor,
  int           baud,
  unsigned int *code
);

MG5UART_STATIC void mg5uart_enable_interrupts(
  int minor,
  int mask
);

/*
 *  mg5uart_set_attributes
 *
 *  This function sets the UART channel to reflect the requested termios
 *  port settings.
 */

MG5UART_STATIC int mg5uart_set_attributes( 
  int minor,
  const struct termios *t
)
{
  unsigned32             pMG5UART_port;
  unsigned32             pMG5UART;
  unsigned int           cmd;
  unsigned int           baudcmd;
  unsigned int           portshift;
  rtems_interrupt_level  Irql;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  /*
   *  Set the baud rate
   */

  if (mg5uart_baud_rate( minor, t->c_cflag, &baudcmd ) == -1)
    return -1;

  /*
   *  Base settings
   */

  /*
   *  Base settings
   */

  cmd = MONGOOSEV_UART_CMD_TX_ENABLE | MONGOOSEV_UART_CMD_TX_ENABLE;

  /*
   *  Parity
   */

  if (t->c_cflag & PARENB) {
    cmd |= MONGOOSEV_UART_CMD_PARITY_ENABLE;
    if (t->c_cflag & PARODD)
      cmd |= MONGOOSEV_UART_CMD_PARITY_ODD;
    else
      cmd |= MONGOOSEV_UART_CMD_PARITY_EVEN;
  } else {
    cmd |= MONGOOSEV_UART_CMD_PARITY_DISABLE;
  }

  /*
   *  Character Size
   */

  if (t->c_cflag & CSIZE) {
    switch (t->c_cflag & CSIZE) {
      case CS5:  
      case CS6:  
      case CS7:  
        return -1;
        break;
      case CS8:
        /* Mongoose-V only supports CS8 */
        break;
 
    }
  } /* else default to CS8 */

  /*
   *  Stop Bits
   */
  
#if 0
  if (t->c_cflag & CSTOPB) {
    /* 2 stop bits not supported by Mongoose-V uart */
    return -1;
  }
#endif

  /*
   *  XXX what about CTS/RTS
   */

  /* XXX */

  /*
   *  Now write the registers
   */

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    portshift = MONGOOSEV_UART0_CMD_SHIFT;
  else
    portshift = MONGOOSEV_UART1_CMD_SHIFT;

  rtems_interrupt_disable(Irql);
    MG5UART_SETREG( pMG5UART, MG5UART_COMMAND_REGISTER, cmd << portshift );
    MG5UART_SETREG( pMG5UART_port, MG5UART_BAUD_RATE, baudcmd );
  rtems_interrupt_enable(Irql);
  return 0;
}

/*
 *  mg5uart_initialize_context
 *
 *  This function sets the default values of the per port context structure.
 */

MG5UART_STATIC void mg5uart_initialize_context(
  int               minor,
  mg5uart_context  *pmg5uartContext
)
{
  int          port;
  unsigned int pMG5UART;
  unsigned int pMG5UART_port;
  
  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  pmg5uartContext->mate = -1;

  for (port=0 ; port<Console_Port_Count ; port++ ) {
    if ( Console_Port_Tbl[port].ulCtrlPort1 == pMG5UART && 
         Console_Port_Tbl[port].ulCtrlPort2 != pMG5UART_port ) {
      pmg5uartContext->mate = port;
      break;
    }
  }

}

/*
 *  mg5uart_init
 *
 *  This function initializes the DUART to a quiecsent state.
 */

MG5UART_STATIC void mg5uart_init(int minor)
{
  unsigned32              pMG5UART_port;
  unsigned32              pMG5UART;
  mg5uart_context        *pmg5uartContext;

  pmg5uartContext = (mg5uart_context *) malloc(sizeof(mg5uart_context));

  Console_Port_Data[minor].pDeviceContext = (void *)pmg5uartContext;

  mg5uart_initialize_context( minor, pmg5uartContext );

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  /*
   *  Reset everything and leave this port disabled.
   */

  MG5UART_SETREG( pMG5UART, 0, MONGOOSEV_UART_CMD_RESET_BOTH_PORTS );

  /*
   *  Disable interrupts on RX and TX for this port
   */

  mg5uart_enable_interrupts( minor, MG5UART_DISABLE_ALL );
}

/*
 *  mg5uart_open
 *
 *  This function opens a port for communication.
 *
 *  Default state is 9600 baud, 8 bits, No parity, and 1 stop bit.
 */

MG5UART_STATIC int mg5uart_open(
  int      major,
  int      minor,
  void    *arg
)
{
  unsigned32             pMG5UART;
  unsigned32             pMG5UART_port;
  unsigned int           vector;
  unsigned int           cmd;
  unsigned int           baudcmd;
  unsigned int           portshift;
  rtems_interrupt_level  Irql;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;
  vector        = Console_Port_Tbl[minor].ulIntVector;

  /* XXX default baud rate could be from configuration table */

  (void) mg5uart_baud_rate( minor, B9600, &baudcmd );

  /*
   *  Set the DUART channel to a default useable state
   *  B9600, 8Nx since there is no stop bit control.
   */

  cmd = MONGOOSEV_UART_CMD_TX_ENABLE | MONGOOSEV_UART_CMD_RX_ENABLE;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    portshift = MONGOOSEV_UART0_CMD_SHIFT;
  else
    portshift = MONGOOSEV_UART1_CMD_SHIFT;

  rtems_interrupt_disable(Irql);
    MG5UART_SETREG( pMG5UART, MG5UART_COMMAND_REGISTER, cmd << portshift );
    MG5UART_SETREG( pMG5UART_port, MG5UART_BAUD_RATE, baudcmd );
  rtems_interrupt_enable(Irql);

  return RTEMS_SUCCESSFUL;
}

/*
 *  mg5uart_close
 *
 *  This function shuts down the requested port.
 */

MG5UART_STATIC int mg5uart_close(
  int      major,
  int      minor,
  void    *arg
)
{
  unsigned32      pMG5UART;
  unsigned32      pMG5UART_port;
  unsigned int    cmd;
  unsigned int    portshift;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  /*
   *  Disable interrupts from this channel and then disable it totally.
   */

  /* XXX interrupts */

  cmd = MONGOOSEV_UART_CMD_TX_DISABLE | MONGOOSEV_UART_CMD_RX_DISABLE;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    portshift = MONGOOSEV_UART0_CMD_SHIFT;
  else
    portshift = MONGOOSEV_UART1_CMD_SHIFT;

  MG5UART_SETREG( pMG5UART, MG5UART_COMMAND_REGISTER, cmd << portshift );

  return(RTEMS_SUCCESSFUL);
}

/* 
 *  mg5uart_write_polled
 *
 *  This routine polls out the requested character.
 */

MG5UART_STATIC void mg5uart_write_polled(
  int   minor, 
  char  c
)
{
  unsigned32              pMG5UART;
  unsigned32              pMG5UART_port;
  unsigned32              status;
  int                     shift;
  int                     timeout;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    shift = MONGOOSEV_UART0_IRQ_SHIFT;
  else
    shift = MONGOOSEV_UART1_IRQ_SHIFT;

  /*
   * wait for transmitter holding register to be empty
   */
  timeout = 1000;
  status = MG5UART_GETREG(pMG5UART, MG5UART_STATUS_REGISTER);
  while ( 1 ) {
    status = MG5UART_GETREG(pMG5UART, MG5UART_STATUS_REGISTER) >> shift;
    
    if ( (status & (MONGOOSEV_UART_TX_READY|MONGOOSEV_UART_TX_EMPTY_0)) ==
            (MONGOOSEV_UART_TX_READY|MONGOOSEV_UART_TX_EMPTY_0) )
      break;

    /*
     * Yield while we wait
     */

#if 0
     if(_System_state_Is_up(_System_state_Get())) {
       rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
     }
#endif
     if(!--timeout) {
       break;
     }
  }

  /*
   * transmit character
   */

  MG5UART_SETREG(pMG5UART_port, MG5UART_TX_BUFFER, c);
}

/*
 *  mg5uart_isr_XXX
 *
 *  This is the single interrupt entry point which parcels interrupts
 *  out to the handlers for specific sources and makes sure that the
 *  shared handler gets the right arguments.
 *
 *  NOTE: Yes .. this is ugly but it provides 5 interrupt source
 *  wrappers which are nearly functionally identical. 
 */


#define __ISR(_TYPE, _OFFSET) \
  MG5UART_STATIC void mg5uart_process_isr_ ## _TYPE ( \
    int  minor \
  ); \
  \
  MG5UART_STATIC rtems_isr mg5uart_isr_ ## _TYPE ( \
    rtems_vector_number vector \
  ) \
  { \
    int     minor; \
   \
    for(minor=0 ; minor<Console_Port_Count ; minor++) { \
      if( Console_Port_Tbl[minor].deviceType == SERIAL_MG5UART && \
          vector == Console_Port_Tbl[minor].ulIntVector + _OFFSET ) { \
        mg5uart_process_isr_ ## _TYPE (minor); \
      } \
    } \
  }

__ISR(rx_frame_error, MG5UART_IRQ_RX_FRAME_ERROR)
__ISR(rx_overrun_error, MG5UART_IRQ_RX_OVERRUN_ERROR)
__ISR(tx_empty, MG5UART_IRQ_TX_EMPTY)
__ISR(tx_ready, MG5UART_IRQ_TX_READY)
__ISR(rx_ready, MG5UART_IRQ_RX_READY)
 

MG5UART_STATIC void mg5uart_process_isr_rx_frame_error(
  int  minor
)
{
  unsigned32              pMG5UART;
  int                     shift;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    shift = MONGOOSEV_UART0_IRQ_SHIFT;
  else
    shift = MONGOOSEV_UART1_IRQ_SHIFT;

  /* now clear the error */

  MG5UART_SETREG(
    pMG5UART,
    MG5UART_STATUS_REGISTER, 
    MONGOOSEV_UART_RX_FRAME_ERROR << shift
  );
}

MG5UART_STATIC void mg5uart_process_isr_rx_overrun_error(
  int  minor
)
{
  unsigned32              pMG5UART;
  int                     shift;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    shift = MONGOOSEV_UART0_IRQ_SHIFT;
  else
    shift = MONGOOSEV_UART1_IRQ_SHIFT;

  /* now clear the error */

  MG5UART_SETREG(
    pMG5UART,
    MG5UART_STATUS_REGISTER,
    MONGOOSEV_UART_RX_OVERRUN_ERROR << shift
  );
}

MG5UART_STATIC void mg5uart_process_tx_isr(
  int        minor,
  unsigned32 source_mask
);

MG5UART_STATIC void mg5uart_process_isr_tx_empty(
  int  minor
)
{
  mg5uart_process_tx_isr( minor, MONGOOSEV_UART_TX_EMPTY );
}

MG5UART_STATIC void mg5uart_process_isr_tx_ready(
  int  minor
)
{
  mg5uart_process_tx_isr( minor, MONGOOSEV_UART_TX_READY );
}

MG5UART_STATIC void mg5uart_process_tx_isr(
  int        minor,
  unsigned32 source_mask
)
{
  unsigned32              pMG5UART;
  int                     shift;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;

  if (!rtems_termios_dequeue_characters(
       Console_Port_Data[minor].termios_data, 1))
    return;


  /*
   *  There are no more characters to transmit so clear the interrupt
   *  source and disable TX interrupts.
   */

  Console_Port_Data[minor].bActive = FALSE;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    shift = MONGOOSEV_UART0_IRQ_SHIFT;
  else
    shift = MONGOOSEV_UART1_IRQ_SHIFT;

  /* now clear the interrupt source */

  MG5UART_SETREG(
    pMG5UART,
    MG5UART_STATUS_REGISTER,
    source_mask << shift
  );

  mg5uart_enable_interrupts(minor, MG5UART_ENABLE_ALL_EXCEPT_TX);

}

MG5UART_STATIC void mg5uart_process_isr_rx_ready(
  int  minor
)
{
  unsigned32              pMG5UART_port;
  unsigned char           c;

  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  c = (unsigned char) MG5UART_GETREG(pMG5UART_port, MG5UART_RX_BUFFER);
  rtems_termios_enqueue_raw_characters(
    Console_Port_Data[minor].termios_data,
    &c,
    1
  );

  /* reading the RX buffer automatically resets the error */ 
}

/*
 *  mg5uart_initialize_interrupts
 *
 *  This routine initializes the console's receive and transmit
 *  ring buffers and loads the appropriate vectors to handle the interrupts.
 */

MG5UART_STATIC void mg5uart_initialize_interrupts(int minor)
{
  unsigned long v;
  mg5uart_init(minor);

  Console_Port_Data[minor].bActive = FALSE;
  v = Console_Port_Tbl[minor].ulIntVector;

  set_vector(mg5uart_isr_rx_frame_error,   v + MG5UART_IRQ_RX_FRAME_ERROR, 1);
  set_vector(mg5uart_isr_rx_overrun_error, v + MG5UART_IRQ_RX_OVERRUN_ERROR, 1);
  set_vector(mg5uart_isr_tx_empty,         v + MG5UART_IRQ_TX_EMPTY, 1);
  set_vector(mg5uart_isr_tx_ready,         v + MG5UART_IRQ_TX_READY, 1);
  set_vector(mg5uart_isr_rx_ready,         v + MG5UART_IRQ_RX_READY, 1);

  mg5uart_enable_interrupts(minor, MG5UART_ENABLE_ALL_EXCEPT_TX);
}

/* 
 *  mg5uart_write_support_int
 *
 *  Console Termios output entry point when using interrupt driven output.
 */

MG5UART_STATIC int mg5uart_write_support_int(
  int         minor, 
  const char *buf, 
  int         len
)
{
  unsigned32      Irql;
  unsigned32      pMG5UART_port;

  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  /*
   *  We are using interrupt driven output and termios only sends us
   *  one character at a time.
   */

  if ( !len )
    return 0;

  /*
   *  Put the character out and enable interrupts if necessary.
   */

  rtems_interrupt_disable(Irql);
    if ( Console_Port_Data[minor].bActive == FALSE ) {
      Console_Port_Data[minor].bActive = TRUE;
      mg5uart_enable_interrupts(minor, MG5UART_ENABLE_ALL);
    }
    MG5UART_SETREG(pMG5UART_port, MG5UART_TX_BUFFER, *buf);
  rtems_interrupt_enable(Irql);

  return 1;
}

/* 
 *  mg5uart_write_support_polled
 *
 *  Console Termios output entry point when using polled output.
 *
 */

MG5UART_STATIC int mg5uart_write_support_polled(
  int         minor, 
  const char *buf, 
  int         len
)
{
  int nwrite = 0;

  /*
   * poll each byte in the string out of the port.
   */
  while (nwrite < len) {
    /*
     * transmit character
     */
    mg5uart_write_polled(minor, *buf++);
    nwrite++;
  }

  /*
   * return the number of bytes written.
   */
  return nwrite;
}

/* 
 *  mg5uart_inbyte_nonblocking_polled 
 *
 *  Console Termios polling input entry point.
 */

MG5UART_STATIC int mg5uart_inbyte_nonblocking_polled( 
  int minor 
)
{
  unsigned32              pMG5UART;
  unsigned32              pMG5UART_port;
  unsigned32              status;
  int                     shift;

  pMG5UART      = Console_Port_Tbl[minor].ulCtrlPort1;
  pMG5UART_port = Console_Port_Tbl[minor].ulCtrlPort2;

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    shift = MONGOOSEV_UART0_IRQ_SHIFT;
  else
    shift = MONGOOSEV_UART1_IRQ_SHIFT;

  status = MG5UART_GETREG(pMG5UART, MG5UART_STATUS_REGISTER) >> shift;
  if ( status & MONGOOSEV_UART_RX_READY ) {
    return (int) MG5UART_GETREG(pMG5UART_port, MG5UART_RX_BUFFER);
  } else {
    return -1;
  }
}

/*
 *  mg5uart_baud_rate
 */

MG5UART_STATIC int mg5uart_baud_rate(
  int           minor,
  int           baud,
  unsigned int *code
)
{
  rtems_unsigned32 clock;
  rtems_unsigned32 tmp_code;
  rtems_unsigned32 baud_requested;

  baud_requested = baud & CBAUD;
  if (!baud_requested)
    baud_requested = B9600;              /* default to 9600 baud */
  
  baud_requested = termios_baud_to_number( B9600 );

  clock = (rtems_unsigned32) Console_Port_Tbl[minor].ulClock;
  if (!clock)
    rtems_fatal_error_occurred(RTEMS_INVALID_NUMBER);

  /*
   *  Formula is Code = round(ClockFrequency / Baud - 1).
   *
   *  Since this is integer math, we will divide by twice the baud and
   *  check the remaining odd bit.
   */

  tmp_code = (clock / (baud_requested * 2));
  if ( tmp_code & 0x01 )
    tmp_code = (tmp_code >> 1) + 1;
  else
    tmp_code = (tmp_code >> 1);

  /*
   *  From section 12.7, "Keep C>100 for best receiver operation."
   *  That is 100 cycles which is not a lot of instructions.  It is
   *  reasonable to think that the Mongoose-V could not keep
   *  up with C < 200.  
   */

  if ( tmp_code < 100 )
    return RTEMS_INVALID_NUMBER;

  /*
   *  upper word is receiver baud and lower word is transmitter baud
   */

  *code = (tmp_code << 16) | tmp_code;
  return 0;
}

/*
 *  mg5uart_enable_interrupts
 *
 *  This function enables specific interrupt sources on the DUART.
 */

MG5UART_STATIC void mg5uart_enable_interrupts(
  int minor,
  int mask
)
{
  unsigned32            pMG5UART;
  unsigned int          shift;

  pMG5UART = Console_Port_Tbl[minor].ulCtrlPort1;

  /*
   *  Enable interrupts on RX and TX -- not break
   */

  if ( Console_Port_Tbl[minor].ulDataPort == MG5UART_UART0 )
    shift = MONGOOSEV_UART0_IRQ_SHIFT;
  else
    shift = MONGOOSEV_UART1_IRQ_SHIFT;

  MG5UART_SETREG(
     pMG5UART,
     MG5UART_INTERRUPT_MASK_REGISTER,
     mask << shift
  );
}

/*
 * Flow control is only supported when using interrupts
 */

console_fns mg5uart_fns =
{
  libchip_serial_default_probe,   /* deviceProbe */
  mg5uart_open,                   /* deviceFirstOpen */
  NULL,                           /* deviceLastClose */
  NULL,                           /* deviceRead */
  mg5uart_write_support_int,      /* deviceWrite */
  mg5uart_initialize_interrupts,  /* deviceInitialize */
  mg5uart_write_polled,           /* deviceWritePolled */
  mg5uart_set_attributes,         /* deviceSetAttributes */
  TRUE                            /* deviceOutputUsesInterrupts */
};

console_fns mg5uart_fns_polled =
{
  libchip_serial_default_probe,        /* deviceProbe */
  mg5uart_open,                        /* deviceFirstOpen */
  mg5uart_close,                       /* deviceLastClose */
  mg5uart_inbyte_nonblocking_polled,   /* deviceRead */
  mg5uart_write_support_polled,        /* deviceWrite */
  mg5uart_init,                        /* deviceInitialize */
  mg5uart_write_polled,                /* deviceWritePolled */
  mg5uart_set_attributes,              /* deviceSetAttributes */
  FALSE,                               /* deviceOutputUsesInterrupts */
};

