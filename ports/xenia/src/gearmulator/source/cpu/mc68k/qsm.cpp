#include "qsm.h"

#include "logging.h"
#include "mc68k.h"

namespace mc68k
{
	constexpr uint16_t g_spcr0_mstrMask		= (1<<15);
	constexpr uint16_t g_spcr0_spbrMask		= 0xf;

	constexpr uint16_t g_spcr1_speMask		= (1<<15);

	constexpr uint16_t g_spcr2_newqpMask	= 0xf;
	constexpr uint16_t g_spcr2_endqpMask	= 0xf00;

	constexpr uint16_t g_spcr2_SpifieMask	= (1<<15);
	constexpr uint16_t g_spcr2_wrenMask		= (1<<14);
	constexpr uint16_t g_spcr2_wrtoMask		= (1<<13);

	constexpr uint16_t g_spcr3_haltMask		= (1<<8);

	constexpr uint16_t g_spsr_cptqpMask		= 0xf;
	constexpr uint16_t g_spsr_spifMask		= (1<<7);
	constexpr uint16_t g_spsr_haltAckMask	= (1<<5);

	constexpr uint32_t g_sciRxDelay = 50;

	constexpr uint8_t g_cmd_dtMask			= (1<<5);
	constexpr uint8_t g_cmd_dsckMask		= (1<<4);

	Qsm::Qsm(Mc68k& _mc68k) : m_mc68k(_mc68k), m_qspi(*this)
	{
		write16(PeriphAddress::Spcr1, 0b0000010000000100);
		write16(PeriphAddress::Qilr,  0b0000000000001111);
		write16(PeriphAddress::SciStatus,    0b110000000);
	}

	void Qsm::write16(PeriphAddress _addr, uint16_t _val)
	{
		const auto prev = read16(_addr);

		PeripheralBase::write16(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::Spcr0:
//			MCLOG("Set SPCR0 to " << MCHEXN(_val, 4));
			return;
		case PeriphAddress::Spcr1:
//			MCLOG("Set SPCR1 to " << MCHEXN(_val, 4));
			// writing the value a control register already has does not affect QSPI operation (MC68331UM 6.3.1.1).
			// The XT restarts its queue with SPCR1 = $8000 while a transfer may still run.
			if(_val == prev)
				return;
			cancelTransmit();
			if(!(prev & g_spcr1_speMask) && (_val & g_spcr1_speMask))
				startTransmit();
			return;
		case PeriphAddress::Spcr2:
//			MCLOG("Set SPCR2 to " << MCHEXN(_val, 4));
			if(spcr1() & g_spcr1_speMask)
				startTransmit();
			break;
		case PeriphAddress::Spcr3:
//			MCLOG("Set SPCR3 to " << MCHEXN(_val, 4));
			if(_val == prev)
				break;
			cancelTransmit();
			// acknowledge halt if halt requested
			if(!(prev & g_spcr3_haltMask) && (_val & g_spcr3_haltMask))
				spsr(spsr() | g_spsr_haltAckMask);
			break;
		case PeriphAddress::Pqspar:
			MCLOG("Set PQSPAR to " << MCHEXN(_val, 4));
			m_portQS.enablePins(~(_val >> 8));
			m_portQS.setDirection(_val & 0xff);
			return;
		case PeriphAddress::SciControl0:
			MCLOG("Set SCCR0 to " << MCHEXN(_val, 4));
			return;
		case PeriphAddress::SciControl1:
//			MCLOG("Set SCCR1 to " << MCHEXN(_val, 4));
			if(bitTest(_val, Sccr1Bits::TransmitInterruptEnable) && !bitTest(prev, Sccr1Bits::TransmitInterruptEnable))
				injectInterrupt(ScsrBits::TransmitDataRegisterEmpty);
			return;
		case PeriphAddress::SciStatus:
			MCLOG("Set SCSR to " << MCHEXN(_val, 4));
			return;
		case PeriphAddress::SciData:
			writeSciData(_val);
			return;
		}
//		MCLOG("write16 addr=" << MCHEXN(_addr, 8) << ", val=" << MCHEXN(_val,4));
	}

	uint16_t Qsm::read16(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::SciStatus:
			return readSciStatus();
		case PeriphAddress::SciData:
			return readSciRX();
		case PeriphAddress::Portqs:
			return m_portQS.read();
		case PeriphAddress::SciControl0:
		case PeriphAddress::SciControl1:
			return PeripheralBase::read16(_addr);
		}

//		MCLOG("read16 addr=" << MCHEXN(_addr, 8) << ", pc=" << MCHEXN(m_mc68k.getPC(), 6));
		return PeripheralBase::read16(_addr);
	}

	void Qsm::write8(PeriphAddress _addr, uint8_t _val)
	{
//		MCLOG("write8 addr=" << MCHEXN(_addr, 8) << ", val=" << MCHEXN(static_cast<int>(_val),2));

		if(_addr == PeriphAddress::SciControl1LSB)
		{
			write16(PeriphAddress::SciControl1, (read16(PeriphAddress::SciControl1) & 0xff00) | _val);
			return;
		}

		const auto prev = read16(_addr);

		PeripheralBase::write8(_addr, _val);

		const auto newVal = read16(_addr);

		switch (_addr)
		{
		case PeriphAddress::Spcr1:
			if(!(prev & g_spcr1_speMask) && (newVal & g_spcr1_speMask))
				startTransmit();
			return;
		case PeriphAddress::Spcr2:
//			MCLOG("Set SPCR2 to " << MCHEXN(_val, 4));
			if(spcr1() & g_spcr1_speMask)
				startTransmit();
			return;
		case PeriphAddress::Spcr3:
			if(newVal == prev)
				return;
			cancelTransmit();
			// acknowledge halt if halt requested
			if(!(prev & g_spcr3_haltMask) && (newVal & g_spcr3_haltMask))
				spsr(spsr() | g_spsr_haltAckMask);
			return;
		case PeriphAddress::Ddrqs:
			MCLOG("Set DDRQS to " << MCHEXN(_val, 2));
			m_portQS.setDirection(_val);
			return;
		case PeriphAddress::Pqspar:
			MCLOG("Set PQSPAR to " << MCHEXN(_val, 2));
			m_portQS.enablePins(~_val);
			return;
		case PeriphAddress::Portqs:
			MCLOG("Set PortQS to " << MCHEXN(_val,2));
			m_portQS.writeTX(_val);
			return;
		case PeriphAddress::SciDataLSB:
			writeSciData(_val);
			return;
		case PeriphAddress::SciData:
			writeSciData(static_cast<uint16_t>(static_cast<uint32_t>(_val) << 8));
			return;
		case PeriphAddress::SciStatus:
			MCLOG("Set SCSR to " << MCHEXN(_val, 2));
			return;
		}
	}

	uint8_t Qsm::read8(const PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::Portqs:
			return m_portQS.read();
		case PeriphAddress::SciStatus:
			return readSciStatus() >> 8;
		case PeriphAddress::SciData:
			return read16(PeriphAddress::SciData) >> 8;
		case PeriphAddress::SciDataLSB:
			return read16(PeriphAddress::SciData) & 0xff;
		case PeriphAddress::SciControl1LSB:
		case PeriphAddress::Qilr:
		case PeriphAddress::Qivr:
		case PeriphAddress::Spsr:
			return PeripheralBase::read8(_addr);
		}
//		MCLOG("read8 addr=" << MCHEXN(_addr, 8) << ", pc=" << MCHEXN(m_mc68k.getPC(), 6));
		return PeripheralBase::read8(_addr);
	}

	void Qsm::injectInterrupt(ScsrBits)
	{
		const uint8_t vector = PeripheralBase::read8(PeriphAddress::Qivr) & 0xfe;
		const auto levelQsci = static_cast<uint8_t>(PeripheralBase::read8(PeriphAddress::Qilr) & 0x7);
		m_mc68k.injectInterrupt(vector, levelQsci);
	}

	void Qsm::exec(const uint32_t _deltaCycles)
	{
		PeripheralBase::exec(_deltaCycles);

		m_qspi.exec();

		if(m_nextQueue != 0xff)
		{
			if(m_spiDelay > _deltaCycles)
			{
				m_spiDelay -= _deltaCycles;
			}
			else
			{
				// the word ended within this instruction, the rest of its cycles already count for the next one
				const auto remainder = _deltaCycles - m_spiDelay;
				m_spiDelay = 0;
				execTransmit();
				m_spiDelay -= std::min(m_spiDelay, remainder);
			}
		}

		// SCI

		if(m_pendingTxDataCounter == 2)
		{
			--m_pendingTxDataCounter;
			set(ScsrBits::TransmitDataRegisterEmpty);

			if(bitTest(Sccr1Bits::TransmitInterruptEnable))
				injectInterrupt(ScsrBits::TransmitDataRegisterEmpty);
		}
		else if(m_pendingTxDataCounter == 1)
		{
			--m_pendingTxDataCounter;

			set(ScsrBits::TransmitComplete);

			if(bitTest(Sccr1Bits::TransmitCompleteInterruptEnable))
				injectInterrupt(ScsrBits::TransmitComplete);
		}

		if(m_sciRxDelay > 0)
		{
			--m_sciRxDelay;
			return;
		}

		if(m_sciRxDataEmpty)
			return;

		if(!bitTest(Sccr1Bits::ReceiverEnable))
		{
//			MCLOG("Discarding SCI data " << MCHEXN(m_sciRxData.front(), 4) << ", receiver not enabled");
//			m_sciRxData.pop_front();
			return;
		}

		if(!bitTest(ScsrBits::ReceiveDataRegisterFull))
		{
			set(ScsrBits::ReceiveDataRegisterFull);

			if(bitTest(Sccr1Bits::ReceiverInterruptEnable))
				injectInterrupt(ScsrBits::ReceiveDataRegisterFull);
		}
	}

	void Qsm::writeSciRX(uint16_t _data)
	{
		std::lock_guard lock(m_mutexSciRx);
		m_sciRxData.push_back(_data);
		m_sciRxDataEmpty = false;
	}

	void Qsm::readSciTX(std::deque<uint16_t>& _dst)
	{
		std::lock_guard lock(m_mutexSciTx);
		m_sciTxData.swap(_dst);
		m_sciTxData.clear();
	}

	void Qsm::setSpiWriteCallback(const SpiTxCallback& _callback)
	{
		m_spiTxCallback = _callback;

		if(!m_spiTxCallback)
			m_spiTxCallback = [](uint16_t, uint8_t) {};
	}

	void Qsm::setSpiWriteFinishCallback(const SpiTxFinishCallback& _callback)
	{
		m_spiTxFinishCallback = _callback;

		if(!m_spiTxFinishCallback)
			m_spiTxFinishCallback = [](uint8_t) {};
	}

	void Qsm::startTransmit(const bool _startAtZero/* = false*/)
	{
		// are we master?
		if(!(spcr0() & g_spcr0_mstrMask))
			return;

		const auto cr3 = spcr3();

		const auto halt = cr3 & g_spcr3_haltMask;

		if(halt)
			return;

		// clear completion flag
		spsr(spsr() & ~g_spsr_spifMask);

		m_nextQueue = _startAtZero ? 0 : (spcr2() & g_spcr2_newqpMask);

		// Do NOT process the SPI queue synchronously — let exec() handle it.
		// This avoids re-entrant SPI processing from the QPic callback.
	}

	void Qsm::execTransmit()
	{
		const auto cr3 = spcr3();

		const auto halt = cr3 & g_spcr3_haltMask;

		if(halt)
			return;

		// In wrap mode, continue processing even when SPIF is set.
		// The firmware will clear SPIF when it reads the data.
		const auto wrap = spcr2() & g_spcr2_wrenMask;
		if(!wrap && (spsr() & g_spsr_spifMask))
			return;

		// the time until this word completes (see qspiWordDelayCycles)
		m_spiDelay = qspiWordDelayCycles(m_nextQueue);

		// push out data
		const auto data = PeripheralBase::read16(transmitRamAddr(m_nextQueue));
		m_spiTxCallback(data, m_nextQueue);

		// update completed queue index
		auto sr = spsr();
		sr &= ~g_spsr_cptqpMask;
		sr |= m_nextQueue;

		spsr(sr);

		// advance to next or end
		++m_nextQueue;

		const auto endqp = (spcr2() & g_spcr2_endqpMask) >> 8;
		const auto finished = m_nextQueue > endqp;

		if(finished)
		{
			finishTransfer();
		}
	}

	void Qsm::cancelTransmit()
	{
		m_nextQueue = 0xff;

		// set completion flag
		spsr(spsr() | g_spsr_spifMask);

		// clear enabled flag
//		auto cr1 = spcr1();
//		cr1 &= ~g_spcr1_speMask;
//		spcr1(cr1);
	}

	uint16_t Qsm::bitTest(uint16_t _value, Sccr1Bits _bit)
	{
		return _value & (1<<static_cast<uint32_t>(_bit));
	}

	uint16_t Qsm::bitTest(Sccr1Bits _bit)
	{
		return bitTest(read16(PeriphAddress::SciControl1), _bit);
	}

	uint16_t Qsm::bitTest(ScsrBits _bit)
	{
		return read16(PeriphAddress::SciStatus) & (1<<static_cast<uint32_t>(_bit));
	}

	void Qsm::clear(ScsrBits _bit)
	{
		auto v = PeripheralBase::read16(PeriphAddress::SciStatus);
		v &= ~(1<<static_cast<uint32_t>(_bit));
		PeripheralBase::write16(PeriphAddress::SciStatus, v);
	}

	void Qsm::set(ScsrBits _bit)
	{
		auto v = PeripheralBase::read16(PeriphAddress::SciStatus);
		v |= (1<<static_cast<uint32_t>(_bit));
		PeripheralBase::write16(PeriphAddress::SciStatus, v);
	}

	uint16_t Qsm::readSciRX()
	{
		std::lock_guard lock(m_mutexSciRx);

		// On real MC68331, reading SCDR consumes the received byte ONLY when
		// RDRF is set; if RDRF is clear it returns the last received value and
		// does NOT pop a fresh byte. Without this gate, an ISR that always reads
		// SCSR+SCDR (e.g. one firing for TDRE) silently consumes and drops queued
		// RX bytes, since its RX-handling path only runs when its captured RDRF
		// bit was set.
		if(!bitTest(ScsrBits::ReceiveDataRegisterFull))
			return m_lastSciRxByte;

		if(m_sciRxData.empty())
		{
//			MCLOG("Empty SCI read");
			return 0;
		}

		clear(ScsrBits::ReceiveDataRegisterFull);
		const auto res = m_sciRxData.front();
		m_sciRxData.pop_front();
		m_sciRxDataEmpty = m_sciRxData.empty();
		m_sciRxDelay = g_sciRxDelay;
		m_lastSciRxByte = res;
		return res;
	}

	void Qsm::finishTransfer()
	{
		m_spiTxFinishCallback(m_nextQueue);

		m_nextQueue = 0xff;

		// set completion flag
		spsr(spsr() | g_spsr_spifMask);

		const auto cr2 = spcr2();
		const auto cr3 = spcr3();

		const auto wrap = cr2 & g_spcr2_wrenMask;
		const auto halt = cr3 & g_spcr3_haltMask;

		if(!wrap || halt)
		{
			auto cr1 = spcr1();
			cr1 &= ~g_spcr1_speMask;
			spcr1(cr1);
		}

		if(cr2 & g_spcr2_SpifieMask)
		{
			const auto vector = PeripheralBase::read8(PeriphAddress::Qivr) | 1;
			const auto levelQspi = static_cast<uint8_t>((PeripheralBase::read8(PeriphAddress::Qilr) >> 3) & 0x7);
			if(!m_mc68k.hasPendingInterrupt(vector, levelQspi))
				m_mc68k.injectInterrupt(vector, levelQspi);
		}

		// In wrap mode, restart the transfer but keep SPIF set.
		// The firmware reads SPIF to know a cycle is done, processes data,
		// and clears SPIF. The QSPI continues running independently.
		if(wrap && !halt)
		{
			const auto wrapToZero = !(cr2 & g_spcr2_wrtoMask);
			// Save and restore SPIF across restart since startTransmit clears it
			const auto savedSpif = spsr() & g_spsr_spifMask;
			startTransmit(wrapToZero);
			if (savedSpif)
				spsr(spsr() | g_spsr_spifMask);

			// Baud-rate delay for the first word of the restarted transfer; without
			// it, exec() sees m_spiDelay==0 and fires immediately (one QSPI IRQ per
			// exec() call). See qspiWordDelayCycles.
			m_spiDelay = qspiWordDelayCycles(m_nextQueue);
		}

		if(halt)
		{
			// acknowledge halt
			spsr(spsr() | g_spsr_haltAckMask);
		}
	}

	// Transfer time of one queue entry in Fsys cycles (MC68331 User's Manual 6.3.4): delay before SCK, BITS bits
	// at Fsys/(2*SPBR), then the delay after transfer. BITS is SPCR0[13:10], 0 meaning 16 (BITSE is not modelled).
	// The delays depend on the entry's command:
	// - DSCK set: DSCKL (SPCR1[14:8], 0 meaning 128) cycles, else half an SCK period
	// - DT set: 32 * DTL (SPCR1[7:0], 0 meaning 256) cycles, else the standard 17
	// Without the delays, a firmware that counts QSPI transfers as its time base ran 6.7% fast.
	uint32_t Qsm::qspiWordDelayCycles(const uint8_t _queueIndex)
	{
		const auto cr0 = spcr0();
		const auto cr1 = spcr1();
		const auto command = PeripheralBase::read8(commandRamAddr(_queueIndex & 0xf));

		const uint32_t spbr = cr0 & 0xff;
		const uint32_t bits = (cr0 >> 10) & 0xf;
		const uint32_t dsckl = (cr1 >> 8) & 0x7f;
		const uint32_t dtl = cr1 & 0xff;

		const uint32_t delayBeforeSck = (command & g_cmd_dsckMask) ? (dsckl ? dsckl : 128) : spbr;
		const uint32_t delayAfterTransfer = (command & g_cmd_dtMask) ? 32 * (dtl ? dtl : 256) : 17;

		return std::max(64u, delayBeforeSck + (bits ? bits : 16) * 2 * spbr + delayAfterTransfer);
	}

	PeriphAddress Qsm::commandRamAddr(const uint8_t _offset)
	{
		return static_cast<PeriphAddress>(static_cast<uint32_t>(PeriphAddress::CommandRam0) + _offset);
	}

	PeriphAddress Qsm::transmitRamAddr(const uint8_t _offset)
	{
		return static_cast<PeriphAddress>(static_cast<uint32_t>(PeriphAddress::TransmitRam0) + (_offset<<1));
	}

	PeriphAddress Qsm::receiveRamAddr(const uint8_t _offset)
	{
		return static_cast<PeriphAddress>(static_cast<uint32_t>(PeriphAddress::ReceiveRam0) + (_offset<<1));
	}

	void Qsm::writeSciData(const uint16_t _data)
	{
		if(!bitTest(Sccr1Bits::TransmitterEnable))
			return;

		{
			std::lock_guard lock(m_mutexSciTx);
			m_sciTxData.push_back(_data);
		}
		m_pendingTxDataCounter = 2;
	}

	uint16_t Qsm::readSciStatus()
	{
		// we're always done with everything
		set(ScsrBits::TransmitDataRegisterEmpty);
		set(ScsrBits::TransmitComplete);
		const auto r = PeripheralBase::read16(PeriphAddress::SciStatus);
//		MCLOG("Read SCSR, res=" << MCHEXN(r, 4));
		return r;
	}
}
