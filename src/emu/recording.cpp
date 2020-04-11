// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    recording.cpp

    Core MAME video routines.

***************************************************************************/

#include "emu.h"
#include "screen.h"
#include "aviio.h"
#include "png.h"


namespace
{
	class avi_movie_recording : public movie_recording
	{
	public:
		avi_movie_recording(screen_device *screen)
			: movie_recording(screen)
		{
		}

		bool initialize(running_machine &machine, std::unique_ptr<emu_file> &&file, int32_t width, int32_t height);
		virtual bool append_video_frame(bitmap_rgb32 &bitmap, const rgb_t *palette, int palette_entries) override;
		virtual bool add_sound_to_recording(const s16 *sound, int numsamples) override;

	private:
		avi_file::ptr       m_avi_file;                 // handle to the open movie file
		u32                 m_avi_frame;                // current movie frame number
	};


	class mng_movie_recording : public movie_recording
	{
	public:
		mng_movie_recording(screen_device *screen, std::map<std::string, std::string> &&info_fields);
		~mng_movie_recording();

		bool initialize(std::unique_ptr<emu_file> &&file, bitmap_t &snap_bitmap);
		virtual bool append_video_frame(bitmap_rgb32 &bitmap, const rgb_t *palette, int palette_entries) override;
		virtual bool add_sound_to_recording(const s16 *sound, int numsamples) override;

	private:
		std::unique_ptr<emu_file>			m_mng_file;           // handle to the open movie file
		u32									m_mng_frame;          // current movie frame number
		std::map<std::string, std::string>	m_info_fields;
	};
};


//**************************************************************************
//  MOVIE RECORDING
//**************************************************************************

//-------------------------------------------------
//  movie_recording - constructor
//-------------------------------------------------

movie_recording::movie_recording(screen_device *screen)
    : m_screen(screen)
{
    m_frame_period = m_screen ? m_screen->frame_period() : attotime::from_hz(screen_device::DEFAULT_FRAME_RATE);
}


//-------------------------------------------------
//  movie_recording - destructor
//-------------------------------------------------

movie_recording::~movie_recording()
{
}


//-------------------------------------------------
//  movie_recording::create - creates a new recording
//	for the specified format
//-------------------------------------------------

movie_recording::ptr movie_recording::create(running_machine &machine, screen_device *screen, format fmt, std::unique_ptr<emu_file> &&file, bitmap_rgb32 &snap_bitmap)
{
	movie_recording::ptr result;
	switch (fmt)
	{
	case movie_recording::format::AVI:
		{
			auto avi_recording = std::make_unique<avi_movie_recording>(screen);
			if (avi_recording->initialize(machine, std::move(file), snap_bitmap.width(), snap_bitmap.height()))
				result = std::move(avi_recording);
		}
		break;

	case movie_recording::format::MNG:
		{
			std::map<std::string, std::string> info_fields;
			info_fields["Software"] = std::string(emulator_info::get_appname()).append(" ").append(emulator_info::get_build_version());
			info_fields["System"] = std::string(machine.system().manufacturer).append(" ").append(machine.system().type.fullname());

			auto mng_recording = std::make_unique<mng_movie_recording>(screen, std::move(info_fields));
			if (mng_recording->initialize(std::move(file), snap_bitmap))
				result = std::move(mng_recording);
		}
		break;

	default:
		throw false;
	}

	// if we successfully create a recording, set the current time and return it
	if (result)
		result->set_next_frame_time(machine.time());
	return result;
}


//-------------------------------------------------
//  movie_recording::format_file_extension
//-------------------------------------------------

const char *movie_recording::format_file_extension(movie_recording::format fmt)
{
	switch (fmt)
	{
		case format::AVI:	return "avi";
		case format::MNG:	return "mng";
		default:			throw false;
	}
}


//-------------------------------------------------
//  avi_movie_recording::initialize
//-------------------------------------------------

bool avi_movie_recording::initialize(running_machine &machine, std::unique_ptr<emu_file> &&file, int32_t width, int32_t height)
{
	// we only use the file we're passed to get the full path
	std::string fullpath = file->fullpath();
	file.reset();

	// reset the state
	m_avi_frame = 0;

	// build up information about this new movie
	avi_file::movie_info info;
	info.video_format = 0;
	info.video_timescale = 1000 * frame_period().as_hz();
	info.video_sampletime = 1000;
	info.video_numsamples = 0;
	info.video_width = width;
	info.video_height = height;
	info.video_depth = 24;

	info.audio_format = 0;
	info.audio_timescale = machine.sample_rate();
	info.audio_sampletime = 1;
	info.audio_numsamples = 0;
	info.audio_channels = 2;
	info.audio_samplebits = 16;
	info.audio_samplerate = machine.sample_rate();

	// create the file
	avi_file::error avierr = avi_file::create(fullpath, info, m_avi_file);
	if (avierr != avi_file::error::NONE)
		osd_printf_error("Error creating AVI: %s\n", avi_file::error_string(avierr));
	return avierr == avi_file::error::NONE;
}


//-------------------------------------------------
//  avi_movie_recording::append_video_frame
//-------------------------------------------------

bool avi_movie_recording::append_video_frame(bitmap_rgb32 &bitmap, const rgb_t *palette, int palette_entries)
{
	avi_file::error avierr = m_avi_file->append_video_frame(bitmap);
	return avierr == avi_file::error::NONE;
}


//-------------------------------------------------
//  avi_movie_recording::append_video_frame
//-------------------------------------------------

 bool avi_movie_recording::add_sound_to_recording(const s16 *sound, int numsamples)
{
	g_profiler.start(PROFILER_MOVIE_REC);

	// write the next frame
	avi_file::error avierr = m_avi_file->append_sound_samples(0, sound + 0, numsamples, 1);
	if (avierr == avi_file::error::NONE)
		avierr = m_avi_file->append_sound_samples(1, sound + 1, numsamples, 1);

	g_profiler.stop();
	return avierr == avi_file::error::NONE;
}


//-------------------------------------------------
//  mng_movie_recording - constructor
//-------------------------------------------------

mng_movie_recording::mng_movie_recording(screen_device *screen, std::map<std::string, std::string> &&info_fields)
	: movie_recording(screen)
	, m_info_fields(std::move(info_fields))
{
}


//-------------------------------------------------
//  mng_movie_recording - destructor
//-------------------------------------------------

mng_movie_recording::~mng_movie_recording()
{
	if (m_mng_file)
		mng_capture_stop(*m_mng_file);
}


//-------------------------------------------------
//  mng_movie_recording::initialize
//-------------------------------------------------

bool mng_movie_recording::initialize(std::unique_ptr<emu_file> &&file, bitmap_t &snap_bitmap)
{
	m_mng_file = std::move(file);
	png_error pngerr = mng_capture_start(*m_mng_file, snap_bitmap, frame_period().as_hz());
	if (pngerr != PNGERR_NONE)
		osd_printf_error("Error capturing MNG, png_error=%d\n", pngerr);
	return pngerr == PNGERR_NONE;
}


//-------------------------------------------------
//  mng_movie_recording::append_video_frame
//-------------------------------------------------

bool mng_movie_recording::append_video_frame(bitmap_rgb32 &bitmap, const rgb_t *palette, int palette_entries)
{
	// set up the text fields in the movie info
	png_info pnginfo;
	if (m_mng_frame == 0)
	{
		for (auto &ent : m_info_fields)
			pnginfo.add_text(ent.first.c_str(), ent.second.c_str());
	}

	png_error error = mng_capture_frame(*m_mng_file, pnginfo, bitmap, palette_entries, palette);
	m_mng_frame++;
	return error == png_error::PNGERR_NONE;
}


//-------------------------------------------------
//  mng_movie_recording::add_sound_to_recording
//-------------------------------------------------

bool mng_movie_recording::add_sound_to_recording(const s16 *sound, int numsamples)
{
	// not supported; do nothing
	return true;
}
