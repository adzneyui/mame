// license:BSD-3-Clause
// copyright-holders:AJR
/**********************************************************************

    Sitronix ST2202 8-Bit Integrated Microcontroller
    Sitronix ST2204 8-Bit Integrated Microcontroller

    Functional blocks:
    * Interrupt controller (11 levels excluding BRK and RESET)
    * GPIO (6 ports, 8 bits each)
    * External bus (up to 7 CS outputs, 48M maximum addressable)
    * Timers/event counters with clocking outputs (2 plus base timer)
    * Programmable sound generator (2 channels plus DAC)
    * LCD controller (ST2202: 240x120 B/W, software gray levels)
                     (ST2204: 320x240 B/W or 240x160 4-gray)
    * Serial peripheral interface
    * UART (built-in BRG; RS-232 and IrDA modes)
    * Direct memory access (1 channel)
    * Power down modes (WAI-0, WAI-1, STP)
    * Watchdog timer
    * Low voltage detector
    * 256K (ST2202) or 512K (ST2204) ROM (may be disabled)
    * 4K (ST2202) or 10K (ST2204) RAM

    Emulation is largely based on documentation for the ST2202. The
    ST2204 is believed to be almost entirely backward compatible.

    Two versions of the ST2204 were manufactured: ST2204A, fabricated
    by TSMC, and ST2204B, fabricated by Hyundai. A PDF document
    describing the differences between these two was once available.

    Reverse-engineered documentation for SS2204's internal registers:
    http://blog.kevtris.org/blogfiles/Game%20King%20Inside.txt

**********************************************************************/

#include "emu.h"
#include "st2204.h"

DEFINE_DEVICE_TYPE(ST2202, st2202_device, "st2202", "Sitronix ST2202 Integrated Microcontroller")
DEFINE_DEVICE_TYPE(ST2204, st2204_device, "st2204", "Sitronix ST2204 Integrated Microcontroller")

st2204_device::st2204_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, address_map_constructor map)
	: st2xxx_device(mconfig, type, tag, owner, clock, map, 26, false) // logical; only 23 address lines are brought out
	, m_tmode{0}
	, m_tcntr{0}
	, m_dms(0)
	, m_dmd(0)
	, m_dcnth(0)
{
}

st2204_device::st2204_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: st2204_device(mconfig, ST2204, tag, owner, clock, address_map_constructor(FUNC(st2204_device::int_map), this))
{
}

st2202_device::st2202_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: st2204_device(mconfig, ST2202, tag, owner, clock, address_map_constructor(FUNC(st2202_device::int_map), this))
{
}

void st2204_device::device_start()
{
	std::unique_ptr<mi_st2204> intf = std::make_unique<mi_st2204>();
	intf->data = &space(AS_DATA);
	intf->dcache = space(AS_DATA).cache<0, 0, ENDIANNESS_LITTLE>();
	intf->irr_enable = false;
	intf->irr = 0;
	intf->prr = 0;
	intf->drr = 0;
	intf->dmr = 0;
	intf->irq_service = false;

	init_base_timer(0x0020);

	save_item(NAME(m_tmode));
	save_item(NAME(m_tcntr));
	save_item(NAME(m_dms));
	save_item(NAME(m_dmd));
	save_item(NAME(m_dcnth));

	mintf = std::move(intf);
	save_common_registers();
	init();

	state_add(ST_IRR, "IRR", downcast<mi_st2204 &>(*mintf).irr).mask(0xff);
	state_add(ST_PRR, "PRR", downcast<mi_st2204 &>(*mintf).prr).mask(0xfff);
	state_add(ST_DRR, "DRR", downcast<mi_st2204 &>(*mintf).drr).mask(0x7ff);
	state_add<u16>(ST_IREQ, "IREQ", [this]() { return m_ireq; }, [this](u16 data) { m_ireq = data; update_irq_state(); }).mask(st2xxx_ireq_mask());
	state_add<u16>(ST_IENA, "IENA", [this]() { return m_iena; }, [this](u16 data) { m_iena = data; update_irq_state(); }).mask(st2xxx_ireq_mask());
	for (int i = 0; i < 5; i++)
	{
		state_add(ST_PDA + i, string_format("PD%c", 'A' + i).c_str(), m_pdata[i]);
		state_add(ST_PCA + i, string_format("PC%c", 'A' + i).c_str(), m_pctrl[i]);
		if (i == 2)
			state_add(ST_PSA + i, string_format("PS%c", 'A' + i).c_str(), m_psel[i]);
		if (i == 2 || i == 3)
			state_add(ST_PFC + i - 2, string_format("PF%c", 'A' + i).c_str(), m_pfun[i - 2]);
	}
	state_add(ST_PDL, "PDL", m_pdata[6]);
	state_add(ST_PCL, "PCL", m_pctrl[6]);
	state_add(ST_PMCR, "PMCR", m_pmcr);
	state_add<u8>(ST_BTEN, "BTEN", [this]() { return m_bten; }, [this](u8 data) { bten_w(data); }).mask(0x1f);
	state_add(ST_BTSR, "BTSR", m_btsr).mask(0x1f);
	state_add(ST_T0M, "T0M", m_tmode[0]).mask(0x37);
	state_add(ST_T0C, "T0C", m_tcntr[0]);
	state_add(ST_T1M, "T1M", m_tmode[1]).mask(0x1f);
	state_add(ST_T1C, "T1C", m_tcntr[1]);
	state_add<u8>(ST_SYS, "SYS", [this]() { return m_sys; }, [this](u8 data) { sys_w(data); });
	state_add(ST_MISC, "MISC", m_misc).mask(st2xxx_misc_mask());
	state_add(ST_LSSA, "LSSA", m_lssa);
	state_add(ST_LVPW, "LVPW", m_lvpw);
	state_add(ST_LXMAX, "LXMAX", m_lxmax);
	state_add(ST_LYMAX, "LYMAX", m_lymax);
	state_add(ST_LPAN, "LPAN", m_lpan).mask(st2xxx_lpan_mask());
	state_add(ST_LCTR, "LCTR", m_lctr).mask(st2xxx_lctr_mask());
	state_add(ST_LCKR, "LCKR", m_lckr).mask(st2xxx_lckr_mask());
	state_add(ST_LFRA, "LFRA", m_lfra).mask(0x3f);
	state_add(ST_LAC, "LAC", m_lac).mask(0x1f);
	state_add(ST_LPWM, "LPWM", m_lpwm).mask(st2xxx_lpwm_mask());
	state_add(ST_DMS, "DMS", m_dms);
	state_add(ST_DMR, "DMR", downcast<mi_st2204 &>(*mintf).dmr).mask(0x7ff);
	state_add(ST_DMD, "DMD", m_dmd);
}

void st2204_device::device_reset()
{
	st2xxx_device::device_reset();

	m_tmode[0] = m_tmode[1] = 0;
	m_tcntr[0] = m_tcntr[1] = 0;
}

const char *st2204_device::st2xxx_irq_name(int i) const
{
	switch (i)
	{
	case 0: return "PC0 edge";
	case 1: return "DAC reload";
	case 2: return "Timer 0";
	case 3: return "Timer 1";
	case 4: return "PA transition";
	case 5: return "Base timer";
	case 6: return "LCD frame";
	case 8: return "SPI TX empty";
	case 9: return "SPI RX ready";
	case 10: return "UART TX";
	case 11: return "UART RX";
	default: return "Reserved";
	}
}

u8 st2204_device::mi_st2204::pread(u16 adr)
{
	u16 bank = irq_service && irr_enable ? irr : prr;
	return data->read_byte(u32(bank ^ 1) << 14 | (adr & 0x3fff));
}

u8 st2204_device::mi_st2204::preadc(u16 adr)
{
	u16 bank = irq_service && irr_enable ? irr : prr;
	return dcache->read_byte(u32(bank ^ 1) << 14 | (adr & 0x3fff));
}

void st2204_device::mi_st2204::pwrite(u16 adr, u8 val)
{
	u16 bank = irq_service && irr_enable ? irr : prr;
	data->write_byte(u32(bank ^ 1) << 14 | (adr & 0x3fff), val);
}

u8 st2204_device::mi_st2204::dread(u16 adr)
{
	return data->read_byte(u32(drr) << 15 | (adr & 0x7fff));
}

u8 st2204_device::mi_st2204::dreadc(u16 adr)
{
	return dcache->read_byte(u32(drr) << 15 | (adr & 0x7fff));
}

void st2204_device::mi_st2204::dwrite(u16 adr, u8 val)
{
	data->write_byte(u32(drr) << 15 | (adr & 0x7fff), val);
}

u8 st2204_device::mi_st2204::read(u16 adr)
{
	return program->read_byte(adr);
}

u8 st2204_device::mi_st2204::read_sync(u16 adr)
{
	return BIT(adr, 15) ? dreadc(adr) : BIT(adr, 14) ? preadc(adr) : cache->read_byte(adr);
}

u8 st2204_device::mi_st2204::read_arg(u16 adr)
{
	return BIT(adr, 15) ? dreadc(adr) : BIT(adr, 14) ? preadc(adr) : cache->read_byte(adr);
}

u8 st2204_device::mi_st2204::read_dma(u16 adr)
{
	if (BIT(adr, 15))
		return dcache->read_byte(u32(dmr) << 15 | (adr & 0x7fff));
	else
		return read(adr);
}

u8 st2204_device::mi_st2204::read_vector(u16 adr)
{
	return pread(adr);
}

void st2204_device::mi_st2204::write(u16 adr, u8 val)
{
	program->write_byte(adr, val);
}

u8 st2204_device::pmcr_r()
{
	return m_pmcr;
}

void st2204_device::pmcr_w(u8 data)
{
	m_pmcr = data;
}

unsigned st2204_device::st2xxx_bt_divider(int n) const
{
	// 2 Hz, 8 Hz, 64 Hz, 256 Hz, 2048 Hz
	if (n < 5)
		return 16384 >> ((n & 1) * 2 + (n >> 1) * 5);
	else
		return 0;
}

u8 st2204_device::t0m_r()
{
	return m_tmode[0];
}

void st2204_device::t0m_w(u8 data)
{
	m_tmode[0] = data & 0x37;
}

u8 st2204_device::t0c_r()
{
	return m_tcntr[0];
}

void st2204_device::t0c_w(u8 data)
{
	m_tcntr[0] = data;
}

u8 st2204_device::t1m_r()
{
	return m_tmode[1];
}

void st2204_device::t1m_w(u8 data)
{
	m_tmode[1] = data & 0x1f;
}

u8 st2204_device::t1c_r()
{
	return m_tcntr[1];
}

void st2204_device::t1c_w(u8 data)
{
	m_tcntr[1] = data;
}

u8 st2204_device::dmsl_r()
{
	return m_dms & 0xff;
}

void st2204_device::dmsl_w(u8 data)
{
	m_dms = (m_dms & 0xff00) | data;
}

u8 st2204_device::dmsh_r()
{
	return m_dms >> 8;
}

void st2204_device::dmsh_w(u8 data)
{
	m_dms = (m_dms & 0x00ff) | u16(data) << 8;
}

u8 st2204_device::dmdl_r()
{
	return m_dmd & 0xff;
}

void st2204_device::dmdl_w(u8 data)
{
	m_dmd = (m_dmd & 0xff00) | data;
}

u8 st2204_device::dmdh_r()
{
	return m_dmd >> 8;
}

void st2204_device::dmdh_w(u8 data)
{
	m_dmd = (m_dmd & 0x00ff) | u16(data) << 8;
}

void st2204_device::dcntl_w(u8 data)
{
	u16 count = data | u16(m_dcnth & 0x0f) << 8;

	// FIXME: not instantaneous (obviously), but takes 2 cycles per transfer while CPU is halted
	mi_st2204 &intf = downcast<mi_st2204 &>(*mintf);
	while (count != 0xffff)
	{
		intf.write(m_dmd, intf.read_dma(m_dms));

		if (m_dms++ == 0xffff)
		{
			// DMR bank increments automatically when source is in data memory
			m_dms = 0x8000;
			intf.dmr = (intf.dmr + 1) & 0x7ff;
		}

		// DMAM inhibits destination increment
		if (!BIT(m_dcnth, 4))
			m_dmd++;

		count--;
	}
}

void st2204_device::dcnth_w(u8 data)
{
	m_dcnth = data & 0x1f;
}

u8 st2204_device::pmem_r(offs_t offset)
{
	return downcast<mi_st2204 &>(*mintf).pread(offset);
}

void st2204_device::pmem_w(offs_t offset, u8 data)
{
	downcast<mi_st2204 &>(*mintf).pwrite(offset, data);
}

u8 st2204_device::dmem_r(offs_t offset)
{
	return downcast<mi_st2204 &>(*mintf).dread(offset);
}

void st2204_device::dmem_w(offs_t offset, u8 data)
{
	downcast<mi_st2204 &>(*mintf).dwrite(offset, data);
}

void st2204_device::common_map(address_map &map)
{
	map(0x0000, 0x0004).rw(FUNC(st2204_device::pdata_r), FUNC(st2204_device::pdata_w));
	map(0x0005, 0x0005).rw(FUNC(st2204_device::psc_r), FUNC(st2204_device::psc_w));
	map(0x0008, 0x000c).rw(FUNC(st2204_device::pctrl_r), FUNC(st2204_device::pctrl_w));
	map(0x000d, 0x000d).rw(FUNC(st2204_device::pfc_r), FUNC(st2204_device::pfc_w));
	map(0x000e, 0x000e).rw(FUNC(st2204_device::pfd_r), FUNC(st2204_device::pfd_w));
	map(0x000f, 0x000f).rw(FUNC(st2204_device::pmcr_r), FUNC(st2204_device::pmcr_w));
	map(0x0020, 0x0020).rw(FUNC(st2204_device::bten_r), FUNC(st2204_device::bten_w));
	map(0x0021, 0x0021).rw(FUNC(st2204_device::btsr_r), FUNC(st2204_device::btclr_all_w));
	map(0x0024, 0x0024).rw(FUNC(st2204_device::t0m_r), FUNC(st2204_device::t0m_w));
	map(0x0025, 0x0025).rw(FUNC(st2204_device::t0c_r), FUNC(st2204_device::t0c_w));
	map(0x0026, 0x0026).rw(FUNC(st2204_device::t1m_r), FUNC(st2204_device::t1m_w));
	map(0x0027, 0x0027).rw(FUNC(st2204_device::t1c_r), FUNC(st2204_device::t1c_w));
	map(0x0028, 0x0028).w(FUNC(st2204_device::dmsl_w));
	map(0x0029, 0x0029).w(FUNC(st2204_device::dmsh_w));
	map(0x002a, 0x002a).w(FUNC(st2204_device::dmdl_w));
	map(0x002b, 0x002b).w(FUNC(st2204_device::dmdh_w));
	map(0x002c, 0x002c).w(FUNC(st2204_device::dcntl_w));
	map(0x002d, 0x002d).w(FUNC(st2204_device::dcnth_w));
	map(0x0030, 0x0030).rw(FUNC(st2204_device::sys_r), FUNC(st2204_device::sys_w));
	map(0x0031, 0x0031).rw(FUNC(st2204_device::irrl_r), FUNC(st2204_device::irrl_w));
	map(0x0032, 0x0032).rw(FUNC(st2204_device::prrl_r), FUNC(st2204_device::prrl_w));
	map(0x0033, 0x0033).rw(FUNC(st2204_device::prrh_r), FUNC(st2204_device::prrh_w));
	map(0x0034, 0x0034).rw(FUNC(st2204_device::drrl_r), FUNC(st2204_device::drrl_w));
	map(0x0035, 0x0035).rw(FUNC(st2204_device::drrh_r), FUNC(st2204_device::drrh_w));
	map(0x0036, 0x0036).rw(FUNC(st2204_device::dmrl_r), FUNC(st2204_device::dmrl_w));
	map(0x0037, 0x0037).rw(FUNC(st2204_device::dmrh_r), FUNC(st2204_device::dmrh_w));
	map(0x0038, 0x0038).rw(FUNC(st2204_device::misc_r), FUNC(st2204_device::misc_w));
	map(0x003c, 0x003c).rw(FUNC(st2204_device::ireql_r), FUNC(st2204_device::ireql_w));
	map(0x003d, 0x003d).rw(FUNC(st2204_device::ireqh_r), FUNC(st2204_device::ireqh_w));
	map(0x003e, 0x003e).rw(FUNC(st2204_device::ienal_r), FUNC(st2204_device::ienal_w));
	map(0x003f, 0x003f).rw(FUNC(st2204_device::ienah_r), FUNC(st2204_device::ienah_w));
	map(0x0040, 0x0040).w(FUNC(st2204_device::lssal_w));
	map(0x0041, 0x0041).w(FUNC(st2204_device::lssah_w));
	map(0x0042, 0x0042).w(FUNC(st2204_device::lvpw_w));
	map(0x0043, 0x0043).rw(FUNC(st2204_device::lxmax_r), FUNC(st2204_device::lxmax_w));
	map(0x0044, 0x0044).rw(FUNC(st2204_device::lymax_r), FUNC(st2204_device::lymax_w));
	map(0x0045, 0x0045).rw(FUNC(st2204_device::lpan_r), FUNC(st2204_device::lpan_w));
	map(0x0047, 0x0047).rw(FUNC(st2204_device::lctr_r), FUNC(st2204_device::lctr_w));
	map(0x0048, 0x0048).w(FUNC(st2204_device::lckr_w));
	map(0x0049, 0x0049).w(FUNC(st2204_device::lfra_w));
	map(0x004a, 0x004a).rw(FUNC(st2204_device::lac_r), FUNC(st2204_device::lac_w));
	map(0x004b, 0x004b).rw(FUNC(st2204_device::lpwm_r), FUNC(st2204_device::lpwm_w));
	map(0x004c, 0x004c).rw(FUNC(st2204_device::pl_r), FUNC(st2204_device::pl_w));
	// PCL is listed as write-only in ST2202 specification, but DynamiDesk suggests otherwise
	map(0x004e, 0x004e).rw(FUNC(st2204_device::pcl_r), FUNC(st2204_device::pcl_w));
	map(0x4000, 0x7fff).rw(FUNC(st2204_device::pmem_r), FUNC(st2204_device::pmem_w));
	map(0x8000, 0xffff).rw(FUNC(st2204_device::dmem_r), FUNC(st2204_device::dmem_w));
}

void st2202_device::int_map(address_map &map)
{	common_map(map);
	map(0x0080, 0x0fff).ram();}

void st2204_device::int_map(address_map &map)
{
	common_map(map);
	// Source/destination registers are supposedly not readable on ST2202, but may be readable here (count register isn't)
	map(0x0028, 0x0028).r(FUNC(st2204_device::dmsl_r));
	map(0x0029, 0x0029).r(FUNC(st2204_device::dmsh_r));
	map(0x002a, 0x002a).r(FUNC(st2204_device::dmdl_r));
	map(0x002b, 0x002b).r(FUNC(st2204_device::dmdh_r));
	map(0x0080, 0x287f).ram(); // 2800-287F possibly not present in earlier versions
}
