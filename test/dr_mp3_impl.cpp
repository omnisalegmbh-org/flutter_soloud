// dr_mp3's implementation, for the standalone native tests that link
// WavStream. The plugin compiles it into src/audiobuffer/mp3_stream_decoder.cpp,
// which drags in the rest of the plugin; these tests need only the decoder.
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#include "../src/soloud/src/audiosource/wav/dr_mp3.h"
