APPNAME = sunvox

BUILD_TYPE = Release
#MAKE_WITH_OBJDUMP = true

MAKE_WITHOUT_GUI = true
MAKE_WITHOUT_MAIN = true

MAKE_WITHOUT_FLAC_ENCODER = true
MAKE_WITHOUT_LIBVORBIS_ENCODER = true
MAKE_WITHOUT_LIBVORBIS_DECODER = true

##
## Used libraries
##

ROOT = ../../
SUNDOG = $(ROOT)lib_sundog/
SUNVOX = $(ROOT)lib_sunvox/
DSP = $(ROOT)lib_dsp/
VORBIS = $(ROOT)lib_vorbis/
MP3 = $(ROOT)lib_mp3/
FLAC = $(ROOT)lib_flac/

include $(SUNDOG)sundog_makefile.inc
include $(SUNVOX)sunvox_makefile.inc
include $(DSP)dsp_makefile.inc
include $(VORBIS)vorbis_makefile.inc
include $(MP3)mp3_makefile.inc
include $(FLAC)flac_makefile.inc

##
## Target configuration
##

SUNDOG_DEFINES = \
    -DMIN_SAMPLE_RATE=44100 \
    -DSUNVOX_LIB \
    -DNOVIDEO \
    -DNOVCAP \
    -DNOLIST \
    -DNOFILEUTILS \
    -DNOIMAGEFORMATS \
    -DNOMIDI

ifneq (,$(findstring linux,$(TARGET_OS)))
ifeq ($(TARGET_OS),$(filter $(TARGET_OS),linux linux_armel linux_raspberry_pi))
    LIBS =
    SUNDOG_DEFINES += \
	-D$(STYPE) \
	-DCOLOR16BITS
ifneq ($(MAKE_WITH_SDL),1)
    SUNDOG_DEFINES += -DJACK_AUDIO -DJACK_INPUT
endif
endif
ifeq ($(TARGET_OS),linux_android16)
    LIBS =
    SUNDOG_DEFINES += \
	-D$(STYPE) \
	-DCOLOR16BITS \
	-DOPENGL
#Android:
install:
	mv -f $(APPNAME).so ../android/sample_project/SunVoxLib/src/main/jniLibs/$(TARGET_ABI)/lib$(APPNAME).so
else
#Not Android:
install:
	mv -f $(APPNAME)$(FILE_EXT) ../linux/lib_$(TARGET_ARCH)$(ARCH_SUFFIX)/$(APPNAME)$(APPNAME_SUFFIX)$(FILE_EXT)
endif
endif

ifeq ($(TARGET_OS),windows)
    LIBS =
    SUNDOG_DEFINES += \
	-DCOLOR32BITS \
	-D$(STYPE)
install:
	mv -f $(APPNAME).dll ../windows/lib_$(TARGET_ARCH)$(ARCH_SUFFIX)/$(APPNAME)$(APPNAME_SUFFIX).dll
endif

ifeq ($(TARGET_OS),macos)
    LIBS =
    SUNDOG_DEFINES += \
	-DCOLOR32BITS \
	-DOPENGL \
	-DPS_STYPE_FLOAT32
install:
	mv -f $(APPNAME).dylib ../macos/lib_$(TARGET_ARCH)/
endif

ifeq ($(TARGET_OS),ios)
    LIBS =
    SUNDOG_DEFINES += \
	-D$(STYPE) \
	-DCOLOR16BITS \
	-DOPENGL
install:
	mv -f $(APPNAME).a ../ios/$(APPNAME)_$(TARGET_ARCH).a
final_install:
	$(IOS_TOOLCHAIN_BIN)/libtool -static ../ios/$(APPNAME)_x86_64.a ../ios/$(APPNAME)_arm64.a -o ../ios/$(APPNAME).a
	rm -f ../ios/$(APPNAME)_x86.a
	rm -f ../ios/$(APPNAME)_x86_64.a
	rm -f ../ios/$(APPNAME)_armv7.a
	rm -f ../ios/$(APPNAME)_armv7s.a
	rm -f ../ios/$(APPNAME)_arm64.a
endif

ifeq ($(TARGET_OS),emscripten)
    LIBS =
    LDFLAGS += \
	-s MODULARIZE=1 -s EXPORT_NAME=SunVoxLib \
	-s EXPORTED_FUNCTIONS='["_sv_audio_callback","_sv_audio_callback2", \
	"_sv_open_slot","_sv_close_slot","_sv_lock_slot","_sv_unlock_slot", \
	"_sv_init","_sv_deinit","_sv_get_sample_rate", "_sv_update_input", \
	"_sv_load_from_memory","_sv_save_to_memory","_sv_play","_sv_play_from_beginning","_sv_stop", \
	"_sv_pause","_sv_resume","_sv_sync_resume", \
	"_sv_set_autostop","_sv_get_autostop","_sv_end_of_song","_sv_rewind","_sv_volume","_sv_set_event_t","_sv_send_event", \
	"_sv_get_current_line","_sv_get_current_line2","_sv_get_current_signal_level", \
	"_sv_get_song_name","_sv_set_song_name","_sv_get_base_version", \
	"_sv_get_song_bpm","_sv_get_song_tpl","_sv_get_song_length_frames","_sv_get_song_length_lines", \
	"_sv_get_time_map", \
	"_sv_new_module","_sv_remove_module","_sv_connect_module","_sv_disconnect_module", \
	"_sv_load_module_from_memory","_sv_sampler_load_from_memory","_sv_metamodule_load_from_memory","_sv_vplayer_load_from_memory", \
	"_sv_sampler_par", \
	"_sv_get_number_of_modules","_sv_find_module","_sv_get_module_flags", \
	"_sv_get_module_inputs","_sv_get_module_outputs", \
	"_sv_get_module_type","_sv_get_module_name","_sv_set_module_name", \
	"_sv_get_module_xy","_sv_set_module_xy", \
	"_sv_get_module_color","_sv_set_module_color", \
	"_sv_get_module_finetune","_sv_get_module_scope2", \
	"_sv_module_curve", \
	"_sv_get_number_of_module_ctls", \
	"_sv_get_module_ctl_name","_sv_get_module_ctl_value","_sv_set_module_ctl_value", \
	"_sv_get_module_ctl_min","_sv_get_module_ctl_max","_sv_get_module_ctl_offset","_sv_get_module_ctl_type","_sv_get_module_ctl_group", \
	"_sv_new_pattern","_sv_remove_pattern", \
	"_sv_get_number_of_patterns", \
	"_sv_find_pattern", \
	"_sv_get_pattern_x","_sv_get_pattern_y","_sv_set_pattern_xy", \
	"_sv_get_pattern_tracks","_sv_get_pattern_lines","_sv_set_pattern_size", \
	"_sv_get_pattern_name","_sv_set_pattern_name", \
	"_sv_get_pattern_data","_sv_set_pattern_event","_sv_get_pattern_event","_sv_pattern_mute", \
	"_sv_set_pattern_event","_sv_get_pattern_event", \
	"_sv_get_ticks","_sv_get_ticks_per_second", \
	"_sv_get_log", \
	"_webaudio_callback", \
	"_malloc","_free"]' \
	-s EXPORTED_RUNTIME_METHODS="[intArrayFromString,allocate,ALLOC_NORMAL,UTF8ToString,getValue]"
    SUNDOG_DEFINES += \
	-D$(STYPE) \
	-DCOLOR16BITS
install:
	mv -f *.js ../js/lib$(APPNAME_SUFFIX)
	mv -f *.wasm ../js/lib$(APPNAME_SUFFIX)
endif

##
## Main rules
##

LIB_SRC += sunvox_lib.cpp
LIB_OBJS = $(LIB_SRC:.cpp=.o)
OBJS += $(LIB_OBJS)

LIB_DEPS = $(wildcard ../main/*.h)

$(LIB_OBJS): %.o: ../main/%.cpp $(LIB_DEPS) $(SD_DEPS)
	$(CXX) $(FINAL_CFLAGS) -c $<
