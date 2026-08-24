LGFX_ROOT := ../.pio/libdeps/cyd/LovyanGFX/src
PROJECT_ROOT := ..

CXX := clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wno-unused-function \
	-Isim_include \
	-I. \
	-I$(LGFX_ROOT) \
	-I$(PROJECT_ROOT)/include \
	-I$(PROJECT_ROOT)/src \
	$(shell sdl2-config --cflags) \
	-DLGFX_USE_V1 \
	-DLGFX_SDL

LDFLAGS := $(shell sdl2-config --libs) -lcurl

# LovyanGFX core sources (only what we need)
LGFX_SRCS := \
	$(LGFX_ROOT)/lgfx/v1/LGFXBase.cpp \
	$(LGFX_ROOT)/lgfx/v1/LGFX_Sprite.cpp \
	$(LGFX_ROOT)/lgfx/v1/LGFX_Button.cpp \
	$(LGFX_ROOT)/lgfx/v1/lgfx_fonts.cpp \
	$(LGFX_ROOT)/lgfx/v1/misc/common_function.cpp \
	$(LGFX_ROOT)/lgfx/v1/misc/pixelcopy.cpp \
	$(LGFX_ROOT)/lgfx/v1/misc/SpriteBuffer.cpp \
	$(LGFX_ROOT)/lgfx/v1/misc/DividedFrameBuffer.cpp \
	$(LGFX_ROOT)/lgfx/v1/panel/Panel_Device.cpp \
	$(LGFX_ROOT)/lgfx/v1/panel/Panel_FrameBufferBase.cpp \
	$(LGFX_ROOT)/lgfx/v1/panel/Panel_HasBuffer.cpp \
	$(LGFX_ROOT)/lgfx/v1/panel/Panel_LCD.cpp \
	$(LGFX_ROOT)/lgfx/v1/platforms/sdl/Panel_sdl.cpp \
	$(LGFX_ROOT)/lgfx/v1/platforms/sdl/common.cpp \
	$(LGFX_ROOT)/lgfx/v1/platforms/framebuffer/Panel_fb.cpp \
	$(LGFX_ROOT)/lgfx/v1/platforms/framebuffer/common.cpp

LGFX_C_SRCS := \
	$(LGFX_ROOT)/lgfx/utility/lgfx_qrcode.c \
	$(LGFX_ROOT)/lgfx/utility/lgfx_pngle.c \
	$(LGFX_ROOT)/lgfx/utility/lgfx_qoi.c \
	$(LGFX_ROOT)/lgfx/utility/lgfx_tjpgd.c \
	$(LGFX_ROOT)/lgfx/utility/lgfx_miniz.c

SIM_SRCS := sim_main.cpp
SIM_C_SRCS := font_stubs.c

BUILD := build
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(LGFX_SRCS))) \
	$(patsubst %.c,$(BUILD)/%.o,$(notdir $(LGFX_C_SRCS))) \
	$(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(SIM_SRCS))) \
	$(patsubst %.c,$(BUILD)/%.o,$(notdir $(SIM_C_SRCS)))

TARGET := $(BUILD)/cyradar-sim

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

# Pattern rules for LGFX C++ sources
$(BUILD)/%.o: $(LGFX_ROOT)/lgfx/v1/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(LGFX_ROOT)/lgfx/v1/misc/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(LGFX_ROOT)/lgfx/v1/panel/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(LGFX_ROOT)/lgfx/v1/platforms/sdl/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(LGFX_ROOT)/lgfx/v1/platforms/framebuffer/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(LGFX_ROOT)/lgfx/utility/%.c | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) -std=c11 -O2 -c -o $@ $<

run: $(TARGET)
	cd $(PROJECT_ROOT) && sim/$(BUILD)/cyradar-sim

clean:
	rm -rf $(BUILD)
