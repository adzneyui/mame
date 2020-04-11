// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    recording.h

    MAME AVI/MNG video recording routines.

***************************************************************************/

#pragma once

#ifndef __EMU_H__
#error Dont include this file directly; include emu.h instead.
#endif

#ifndef MAME_EMU_RECORDING_H
#define MAME_EMU_RECORDING_H

#include <memory>

#include "attotime.h"
#include "palette.h"

class screen_device;


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> movie_recording

class movie_recording
{
public:
	// movie format options
	enum class format
	{
		MNG,
		AVI
	};

	typedef std::unique_ptr<movie_recording> ptr;

	// dtor
	virtual ~movie_recording();

	// accessors
	screen_device *screen()					{ return m_screen; }
	attotime frame_period()					{ return m_frame_period; }
	void set_next_frame_time(attotime time)	{ m_next_frame_time = time; }
	attotime next_frame_time() const		{ return m_next_frame_time; }

	// virtuals
	virtual bool append_video_frame(bitmap_rgb32 &bitmap, const rgb_t *palette, int palette_entries) = 0;
	virtual bool add_sound_to_recording(const s16 *sound, int numsamples) = 0;

	// statics
	static movie_recording::ptr create(running_machine &machine, screen_device *screen, format fmt, std::unique_ptr<emu_file> &&file, bitmap_rgb32 &snap_bitmap);
	static const char *format_file_extension(format fmt);

protected:
	// ctor
	movie_recording(screen_device *screen);
	movie_recording(const movie_recording &) = delete;
	movie_recording(movie_recording &&) = delete;

private:
	screen_device *	m_screen;				// screen associated with this movie (can be nullptr)
	attotime		m_frame_period;			// time of frame period
	attotime		m_next_frame_time;      // time of next frame
};


#endif // MAME_EMU_RECORDING_H
