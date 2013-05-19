/* Name: net.c
 * Author: .
 * Copyright: Arduino
 * License: GPL http://www.gnu.org/licenses/gpl-2.0.html
 * Project: eboot
 * Network and W5100 chip support
 * Version: 0.1 tftp / flashing functional
 */

#include <avr/io.h>
#include <avr/eeprom.h>

#include "net.h"
#include "neteeprom.h"
#include "pin_defs.h"
#include "serial.h"
#include "debug.h"
#include "debug_net.h"

#define SS_LOW()	ETH_PORT &= ~_BV(ETH_SS)
#define SS_HIGH()	ETH_PORT |= _BV(ETH_SS)

uint8_t registerBuffer[REGISTER_BLOCK_SIZE] = {
	0x80,         // MR Mode - reset device

	// EEPROM block starts here
	GW_ADDR,      // GWR Gateway IP Address Register
	SUB_MASK,  // SUBR Subnet Mask Register
	MAC_ADDR,     // SHAR Source Hardware Address Register
	IP_ADDR,      // SIPR Source IP Address Register
	// EEPROM block ends here

	0, 0,         // Reserved locations
	0,            // IR Interrupt Register
	0,            // IMR Interrupt Mask Register
	0x07, 0xd0,   // RTR Retry Time-value Register
	0x80,         // RCR Retry Count Register
	0x55,         // RMSR Rx Memory Size Register, 2K per socket
	0x55          // TMSR Tx Memory Size Register, 2K per socket
};


/** Send uint8_t to Ethernet controller */
void netWriteReg(uint16_t address, uint8_t value)
{
	DBG_NET_EX(
	    tracePGMlnNet(mDebugNet_NWREG);
	    tracenum(address);
	    tracePGM(mDebugNet_COMMA);
	    tracenum(value);
	)

	SPCR = _BV(SPE) | _BV(MSTR); // Set SPI as master
	SS_LOW();

	SPDR = SPI_WRITE;
	while(!(SPSR & _BV(SPIF)));

	SPDR = address >> 8;
	while(!(SPSR & _BV(SPIF)));

	SPDR = address & 0xff;
	while(!(SPSR & _BV(SPIF)));

	SPDR = value;
	while(!(SPSR & _BV(SPIF)));

	SS_HIGH();
	SPCR = 0; // Turn off SPI
}


/** Read uint8_t from Ethernet controller */
uint8_t netReadReg(uint16_t address)
{
#if defined(SPAM_ME)
	DBG_NET_EX(
	    tracePGMlnNet(mDebugNet_NRREG);
	    tracenum(address);
	)
#endif

	uint8_t returnValue;

	SPCR = _BV(SPE) | _BV(MSTR);
	SS_LOW();

	SPDR = SPI_READ;
	while(!(SPSR & _BV(SPIF)));

	SPDR = address >> 8;
	while(!(SPSR & _BV(SPIF)));

	SPDR = address & 0xff;
	while(!(SPSR & _BV(SPIF)));

	SPDR = 0;
	while(!(SPSR & _BV(SPIF)));

	SS_HIGH();
	returnValue = SPDR;
	SPCR = 0;

	return(returnValue);
}

uint16_t netReadWord(uint16_t address)
{
	// Read uint16_t from Ethernet controller
	return((netReadReg(address) << 8) | netReadReg(address + 1));
}

void netWriteWord(uint16_t address, uint16_t value)
{
	// Write uint16_t to Ethernet controller
	netWriteReg(address++, value >> 8);
	netWriteReg(address, value & 0xff);
}

void netInit(void)
{
	uint8_t i;

	/**
	 * Set up pins to communicate with W5100 chip.
	 * At this stage all pins are set to HIGH. This in fact DISABLES SPI for both Ethernet and SD.
	 * SS pin for ethernet is pulled low just in time for reading or writing data inside those
	 * functions. */

	/** Set SPI pins high */
	SPI_PORT = _BV(SCK) | _BV(MISO) | _BV(MOSI) | _BV(SS);
	/** Set SPI pins as output */
	SPI_DDR = _BV(SCK) | _BV(MOSI) | _BV(SS);

#if (ETH_SS != SS)
	/** Initialize extra SS pin used in some boards (mega) */
	/** Set ethernet SS high */
	ETH_PORT |= _BV(ETH_SS);
	/** Set ethernet SS as output */
	ETH_DDR |= _BV(ETH_SS);
#endif

	/** Disable SD card */
	/** Set SD SS pin high */
	SD_PORT |= _BV(SD_SS);
	/** Set SD SS pin as output */
	SD_DDR |= _BV(SD_SS);

#if (LED != SCK)
	/** Set up pins to flash the onboard led */
	/** Set led pin to high */
	LED_PORT |= _BV(LED);
	/** Set led pin as output */
	LED_DDR |= _BV(LED);
#endif

	/* Set up SPI
	 * Set the Double SPI Speed Bit */
	SPSR = (1 << SPI2X);

	/* Pull in altered network settings, if available,
	 * from AVR EEPROM (if signature bytes are set) */
	if((eeprom_read_byte(EEPROM_SIG_1) == EEPROM_SIG_1_VALUE)
		&& (eeprom_read_byte(EEPROM_SIG_2) == EEPROM_SIG_2_VALUE)) {

		for(i = 0; i < EEPROM_SETTINGS_SIZE; i++)
			registerBuffer[i + 1] = eeprom_read_byte(EEPROM_DATA + i);

		DBG_NET(tracePGMlnNet(mDebugNet_EEPROM);)

	} DBG_NET(
		else tracePGMlnNet(mDebugNet_BUILTIN);
	)


	DBG_NET(
		tracePGMlnNet(mDebugNet_ADDR);
		for(i = 15; i < 19; i++) {
			tracenet(registerBuffer[i]);
			if(i != 18) putch(0x2E);
		}
		tracePGMlnNet(mDebugNet_SUBN);
		for(i = 5; i < 9; i++) {
			tracenet(registerBuffer[i]);
			if(i != 8) putch(0x2E);
		}
		tracePGMlnNet(mDebugNet_GW);
		for(i = 1; i < 5; i++) {
			tracenet(registerBuffer[i]);
			if(i != 4) putch(0x2E);
		}
		tracePGMlnNet(mDebugNet_MAC);
		for(i = 9; i < 15; i++) {
			tracenet(registerBuffer[i]);
			if(i != 14) putch(0x2E);
		}
	)

	/** Configure Wiznet chip. Network settings */
	for(i = 0; i < REGISTER_BLOCK_SIZE; i++)
		netWriteReg(i, registerBuffer[i]);

	DBG_NET(tracePGMlnNet(mDebugNet_DONE);)
}

// kate: indent-mode cstyle; indent-width 4; replace-tabs off; tab-width 4;

