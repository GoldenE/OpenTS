#include "parity.h"
#include "lcw.h"
#include "soscomp.h"
#include "vqalib/cmp.h"
#include "vqalib/unvq.h"
#include <array>
#include <cstring>
#include <stdexcept>

extern "C" {
unsigned short * HicolorTable;
unsigned long Portable_VQA_LCW_Uncompress(char const *, char *, unsigned long);
long Portable_AudioUnzap(void *, void *, long);
void Portable_sosCODECInitStream(_SOS_COMPRESS_INFO *);
unsigned long Portable_sosCODECDecompressData(_SOS_COMPRESS_INFO *, unsigned long);
void Portable_General_sosCODECInitStream(_SOS_COMPRESS_INFO *);
unsigned long Portable_General_sosCODECDecompressData(_SOS_COMPRESS_INFO *, unsigned long);
void Portable_VQA_sosCODECInitStream(_VQA_SOS_COMPRESS_INFO *);
void Portable_VQA_sosCODECDecompressData(void *, void *, unsigned short, unsigned short, unsigned long, _VQA_SOS_COMPRESS_INFO *);
#define DECLARE_UNVQ(name) void Portable_##name(unsigned char *, unsigned char *, unsigned char *, unsigned long, unsigned long, unsigned long)
DECLARE_UNVQ(ASM_UnVQ1_C1_TABLE);
DECLARE_UNVQ(ASM_UnVQ1_C1_TABLE_ALT);
DECLARE_UNVQ(ASM_UnVQ1_C1_4x4);
DECLARE_UNVQ(ASM_UnVQ_4x2);
DECLARE_UNVQ(ASM_UnVQ_4x4);
DECLARE_UNVQ(ASM_UnVQ_4x4_HALF);
}
#if OPENTS_ASM_REFERENCE
int Reference_LCW_Comp(void const *, void *, int);
#endif

using Buffer = std::vector<std::uint8_t>;
void Word(Buffer & bytes, std::uint32_t value)
{
	for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void State(Buffer & output, _SOS_COMPRESS_INFO const & info)
{
	Word(output, info.dwSampleIndex); Word(output, info.dwPredicted); Word(output, info.dwDifference);
	Word(output, info.wCodeBuf); Word(output, info.wCode); Word(output, info.wStep); Word(output, info.wIndex);
	Word(output, info.dwSampleIndex2); Word(output, info.dwPredicted2); Word(output, info.dwDifference2);
	Word(output, info.wCodeBuf2); Word(output, info.wCode2); Word(output, info.wStep2); Word(output, info.wIndex2);
}

void LCW(asmparity::Context & context)
{
	{
		unsigned char input = 43;
		std::array<unsigned char, 4> output{};
		if (LCW_Comp(&input, output.data(), 0) != 1 || output[0] != 0x80) throw std::runtime_error("Empty LCW stream failed");
		if (LCW_Comp(&input, output.data(), 1) != 3 || output[0] != 0x81 || output[1] != input || output[2] != 0x80) throw std::runtime_error("Single-byte LCW stream failed");
	}
	for (unsigned seed = 1; seed <= 204; ++seed) {
		asmparity::Random random(seed);
		unsigned length = seed <= 12 ? seed + 1 : 65 + random.Next() % 4096;
		constexpr unsigned BOUNDARIES[] = {2,3,63,64,65,66,127,128,4095,4096,8192,65535};
		if (seed > 180) length = BOUNDARIES[(seed - 181) % 12];
		Buffer input(length + 128, 0), actual(length * 2 + 128, 0xcc), reference(actual);
		random.Fill(input);
		for (unsigned i = 0; i < length; ++i) {
			if (seed % 4 == 0) input[i] = static_cast<std::uint8_t>(i % 17);
			if (seed % 4 == 1) input[i] = static_cast<std::uint8_t>((i / 87) % 4);
			if (seed % 4 == 2) input[i] &= 7;
			if (seed > 180) input[i] = seed <= 192 ? static_cast<std::uint8_t>(i % 17) : 77;
		}
		int result = LCW_Comp(input.data(), actual.data(), length);
		actual.resize(result);
#if OPENTS_ASM_REFERENCE
		int original = Reference_LCW_Comp(input.data(), reference.data(), length);
		reference.resize(original);
#endif
		Buffer serialized = input;
		Word(serialized, length);
		context.Check("LCW_Comp", seed, serialized, actual, reference);
		Buffer decoded(length + 64, 0);
		int count = LCW_Uncomp(actual.data(), decoded.data(), length);
		if (count != length || std::memcmp(input.data(), decoded.data(), length)) throw std::runtime_error("LCW round trip failed");
	}
	for (unsigned relative = 0; relative < 2; ++relative) {
		Buffer input;
		if (relative) input.push_back(0);
		Buffer commands = {0x84, 10,20,30,40, 0x00,4, 0xc1,0,0, 0xfe,70,0,90, 0xff,80,0,0,0, 0x80};
		commands[8] = relative ? 7 : 0;
		commands[17] = relative ? 1 : 0;
		input.insert(input.end(), commands.begin(), commands.end());
		for (unsigned limit = 0; limit <= 180; ++limit) {
			Buffer actual(256,0xcc), reference(actual), serialized = input;
			Word(serialized, limit);
			auto count = Portable_VQA_LCW_Uncompress(reinterpret_cast<char const *>(input.data()), reinterpret_cast<char *>(actual.data()), limit);
			Word(actual, count);
#if OPENTS_ASM_REFERENCE
			auto original = VQA_LCW_Uncompress(reinterpret_cast<char const *>(input.data()), reinterpret_cast<char *>(reference.data()), limit);
			Word(reference, original);
#endif
			context.Check(relative ? "VQA_LCW_relative" : "VQA_LCW_absolute", limit, serialized, actual, reference);
		}
	}
}

void Audio(asmparity::Context & context)
{
	for (unsigned index = 0; index <= 88; ++index) {
		for (unsigned code = 0; code < 16; ++code) {
			Buffer source(4, static_cast<std::uint8_t>(code)), actual(8, 0xcc), reference(actual), input = source;
			_SOS_COMPRESS_INFO a{}, b{};
			a.wBitSize = 16; a.wChannels = 1; a.wIndex = static_cast<short>(index * 32);
			a.dwPredicted = code & 8 ? -32760 : 32760;
			b = a;
			Word(input, a.wIndex); Word(input, a.dwPredicted);
			a.lpSource = b.lpSource = reinterpret_cast<char *>(source.data());
			a.lpDest = reinterpret_cast<char *>(actual.data()); b.lpDest = reinterpret_cast<char *>(reference.data());
			Portable_sosCODECDecompressData(&a, 2); State(actual, a);
#if OPENTS_ASM_REFERENCE
			sosCODECDecompressData(&b, 2); State(reference, b);
#endif
			context.Check("SOS_all_steps", index * 16 + code, input, actual, reference);
		}
	}
	for (unsigned seed = 1; seed <= 64; ++seed) {
		asmparity::Random random(seed);
		Buffer source(512); random.Fill(source);
		for (unsigned type = 0; type < 5; ++type) {
			Buffer actual, reference;
			_SOS_COMPRESS_INFO a{}, b{};
			a.wBitSize = type < 3 ? 16 : 8;
			a.wChannels = (type == 2 || type == 4) ? 2 : 1;
			b = a;
			if (!type) Portable_sosCODECInitStream(&a); else Portable_General_sosCODECInitStream(&a);
#if OPENTS_ASM_REFERENCE
			if (!type) sosCODECInitStream(&b); else General_sosCODECInitStream(&b);
#endif
			if (seed % 3 == 0) {
				a.dwPredicted = b.dwPredicted = 32760;
				a.dwPredicted2 = b.dwPredicted2 = -32760;
				a.wIndex = b.wIndex = a.wIndex2 = b.wIndex2 = type ? 88 : 88 * 32;
				a.wStep = b.wStep = a.wStep2 = b.wStep2 = 32767;
			}
			for (unsigned frame = 0; frame < 5; ++frame) {
				unsigned bytes = a.wChannels * (a.wBitSize / 8) * (1 + (seed + frame) % 65);
				Buffer out(bytes + 32, 0xcc), ref(out);
				a.lpSource = b.lpSource = reinterpret_cast<char *>(source.data() + frame * 3);
				a.lpDest = reinterpret_cast<char *>(out.data()); b.lpDest = reinterpret_cast<char *>(ref.data());
				auto result = !type ? Portable_sosCODECDecompressData(&a, bytes) : Portable_General_sosCODECDecompressData(&a, bytes);
				actual.insert(actual.end(), out.begin(), out.end()); Word(actual, result); State(actual, a);
#if OPENTS_ASM_REFERENCE
				auto original = !type ? sosCODECDecompressData(&b, bytes) : General_sosCODECDecompressData(&b, bytes);
				reference.insert(reference.end(), ref.begin(), ref.end()); Word(reference, original); State(reference, b);
#endif
			}
			context.Check("SOS_" + std::to_string(type), seed, source, actual, reference);
		}
		for (unsigned channels = 1; channels <= 2; ++channels) {
			_VQA_SOS_COMPRESS_INFO a{}, b{};
			Portable_VQA_sosCODECInitStream(&a);
#if OPENTS_ASM_REFERENCE
			VQA_sosCODECInitStream(&b);
#endif
			if (seed % 3 == 0) {
				a.dwPredicted = b.dwPredicted = -32760;
				a.dwPredicted2 = b.dwPredicted2 = 32760;
				a.wIndex = b.wIndex = a.wIndex2 = b.wIndex2 = 88 * 32;
			}
			Buffer actual, reference;
			for (unsigned frame = 0; frame < 5; ++frame) {
				unsigned bytes = channels * 2 * (1 + (seed + frame) % 65);
				Buffer out(bytes + 32,0xcc), ref(out);
				Portable_VQA_sosCODECDecompressData(source.data()+frame*3, out.data(),16,channels,bytes,&a);
				actual.insert(actual.end(),out.begin(),out.end());
				Word(actual,a.dwPredicted); Word(actual,a.wIndex); Word(actual,a.dwPredicted2); Word(actual,a.wIndex2);
#if OPENTS_ASM_REFERENCE
				VQA_sosCODECDecompressData(source.data()+frame*3, ref.data(),16,channels,bytes,&b);
				reference.insert(reference.end(),ref.begin(),ref.end());
				Word(reference,b.dwPredicted); Word(reference,b.wIndex); Word(reference,b.dwPredicted2); Word(reference,b.wIndex2);
#endif
			}
			context.Check("VQA_SOS_" + std::to_string(channels), seed, source, actual, reference);
		}
	}
	for (unsigned opcode = 0; opcode < 256; ++opcode) {
		Buffer input(128, static_cast<std::uint8_t>((opcode * 17) ^ 0x96)); input[0] = static_cast<std::uint8_t>(opcode);
		Buffer actual(320,0xcc), reference(actual);
		long result = Portable_AudioUnzap(input.data(),actual.data(),1);
		Word(actual,result);
#if OPENTS_ASM_REFERENCE
		Word(reference,AudioUnzap(input.data(),reference.data(),1));
#endif
		context.Check("AudioUnzap_command",opcode,input,actual,reference);
	}
	for (unsigned seed = 1; seed <= 32; ++seed) {
		asmparity::Random random(seed);
		Buffer input(8192); random.Fill(input);
		Buffer actual(4096 + 320, 0xcc), reference(actual);
		Word(actual, Portable_AudioUnzap(input.data(), actual.data(), 4096));
#if OPENTS_ASM_REFERENCE
		Word(reference, AudioUnzap(input.data(), reference.data(), 4096));
#endif
		context.Check("AudioUnzap_stream", seed, input, actual, reference);
	}
}

void VQ(asmparity::Context & context)
{
	using Decoder = void (*)(unsigned char *, unsigned char *, unsigned char *, unsigned long, unsigned long, unsigned long);
	Decoder portable[] = {Portable_ASM_UnVQ1_C1_TABLE,Portable_ASM_UnVQ1_C1_TABLE_ALT,Portable_ASM_UnVQ1_C1_4x4,Portable_ASM_UnVQ_4x2,Portable_ASM_UnVQ_4x4,Portable_ASM_UnVQ_4x4_HALF};
#if OPENTS_ASM_REFERENCE
	Decoder original[] = {ASM_UnVQ1_C1_TABLE,ASM_UnVQ1_C1_TABLE_ALT,ASM_UnVQ1_C1_4x4,ASM_UnVQ_4x2,ASM_UnVQ_4x4,ASM_UnVQ_4x4_HALF};
#endif
	std::array<unsigned short,32768> table;
	for (unsigned i=0;i<table.size();++i) table[i]=static_cast<unsigned short>((i*313)^0x369c);
	HicolorTable=table.data();
	for (unsigned seed=1;seed<=32;++seed) {
		asmparity::Random random(seed);
		Buffer codebook(1024); random.Fill(codebook);
		for (unsigned mode=0;mode<6;++mode) {
			Buffer pointers(30);
			for (unsigned i=0;i<15;++i) {
				unsigned index = random.Next()%32;
				if (i%3==0) index = mode<3 ? 0x8000|(random.Next()&0x7fff) : 0xff00|(random.Next()&255);
				pointers[i]=static_cast<unsigned char>(index); pointers[i+15]=static_cast<unsigned char>(index>>8);
			}
			Buffer actual(2048,0xcc), reference(actual), input=codebook;
			input.insert(input.end(),pointers.begin(),pointers.end());
			portable[mode](codebook.data(),pointers.data(),actual.data()+3,5,3,27);
#if OPENTS_ASM_REFERENCE
			original[mode](codebook.data(),pointers.data(),reference.data()+3,5,3,27);
#endif
			context.Check("UnVQ_"+std::to_string(mode),seed,input,actual,reference);
		}
	}
}

void Run_Suite(asmparity::Context & context)
{
	LCW(context);
	Audio(context);
	VQ(context);
}
