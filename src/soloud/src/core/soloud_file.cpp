/*
SoLoud audio engine
Copyright (c) 2013-2015 Jari Komppa

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#undef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

// 64-bit stdio offsets on every platform that has them. Must precede the first
// system include on 32-bit Linux and Android, where `off_t` is otherwise 32-bit.
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include "soloud.h"
#include "soloud_file.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace SoLoud
{
	// Large-file seek and tell. `fseek`/`ftell` take a `long`, which is 32-bit on
	// Windows and on every 32-bit ABI, so a stream past 2 GiB could neither be
	// positioned nor asked where it was - and a multi-hour lossless stem is well
	// past that. Each platform has a 64-bit pair; this picks it.
	static int soloud_fseek64(FILE *aFile, long long aOffset, int aWhence)
	{
#if defined(_WIN32)
		return _fseeki64(aFile, aOffset, aWhence);
#elif defined(__ANDROID__) && !defined(__LP64__) && __ANDROID_API__ < 24
		// 32-bit bionic before API 24 has no 64-bit stdio seek at all. Keep the
		// 32-bit call there rather than fail to build; those devices keep the old
		// 2 GiB limit.
		return fseek(aFile, (long)aOffset, aWhence);
#else
		return fseeko(aFile, (off_t)aOffset, aWhence);
#endif
	}

	static long long soloud_ftell64(FILE *aFile)
	{
#if defined(_WIN32)
		return _ftelli64(aFile);
#elif defined(__ANDROID__) && !defined(__LP64__) && __ANDROID_API__ < 24
		return ftell(aFile);
#else
		return (long long)ftello(aFile);
#endif
	}

	unsigned int File::read8()
	{
		unsigned char d = 0;
		read((unsigned char*)&d, 1);
		return d;
	}

	unsigned int File::read16()
	{
		unsigned short d = 0;
		read((unsigned char*)&d, 2);
		return d;
	}

	unsigned int File::read32()
	{
		unsigned int d = 0;
		read((unsigned char*)&d, 4);
		return d;
	}

DiskFile::DiskFile(FILE *fp):
mFileHandle(fp)
{

}

	unsigned int DiskFile::read(unsigned char *aDst, unsigned int aBytes)
	{
		return (unsigned int)fread(aDst, 1, aBytes, mFileHandle);
	}

	unsigned long long DiskFile::length()
	{
		if (!mFileHandle)
			return 0;
		long long pos = soloud_ftell64(mFileHandle);
		soloud_fseek64(mFileHandle, 0, SEEK_END);
		long long len = soloud_ftell64(mFileHandle);
		soloud_fseek64(mFileHandle, pos < 0 ? 0 : pos, SEEK_SET);
		return len < 0 ? 0 : (unsigned long long)len;
	}

	void DiskFile::seek(long long aOffset)
	{
		if (!mFileHandle)
			return;
		soloud_fseek64(mFileHandle, aOffset < 0 ? 0 : aOffset, SEEK_SET);
	}

	unsigned long long DiskFile::pos()
	{
		if (!mFileHandle)
			return 0;
		long long pos = soloud_ftell64(mFileHandle);
		return pos < 0 ? 0 : (unsigned long long)pos;
	}

	FILE *DiskFile::getFilePtr()
	{
		return mFileHandle;
	}

	DiskFile::~DiskFile()
	{
		if (mFileHandle)
			fclose(mFileHandle);
	}

	DiskFile::DiskFile()
	{
		mFileHandle = 0;
	}

	result DiskFile::open(const char *aFilename)
	{
		if (!aFilename)
			return INVALID_PARAMETER;
#ifdef _WIN32
		// On Windows, convert UTF-8 to wide string and use _wfopen
		int wideLen = MultiByteToWideChar(CP_UTF8, 0, aFilename, -1, NULL, 0);
		if (wideLen == 0)
			return FILE_NOT_FOUND;
		wchar_t *wideName = new wchar_t[wideLen];
		if (MultiByteToWideChar(CP_UTF8, 0, aFilename, -1, wideName, wideLen) == 0)
		{
			delete[] wideName;
			return FILE_NOT_FOUND;
		}
		mFileHandle = _wfopen(wideName, L"rb");
		delete[] wideName;
#else
		mFileHandle = fopen(aFilename, "rb");
#endif
		if (!mFileHandle)
			return FILE_NOT_FOUND;
		return SO_NO_ERROR;
	}

	int DiskFile::eof()
	{
		return feof(mFileHandle);
	}



	unsigned int MemoryFile::read(unsigned char *aDst, unsigned int aBytes)
	{
		if (mOffset + aBytes >= mDataLength)
			aBytes = mDataLength - mOffset;

		memcpy(aDst, mDataPtr + mOffset, aBytes);
		mOffset += aBytes;

		return aBytes;
	}

	unsigned long long MemoryFile::length()
	{
		return mDataLength;
	}

	void MemoryFile::seek(long long aOffset)
	{
		// In-memory data is bounded by a 32-bit length (see openToMem), so the
		// wide offset only needs clamping into that range. A negative offset is
		// from the end, as before.
		long long target = aOffset >= 0 ? aOffset : (long long)mDataLength + aOffset;
		if (target < 0 || mDataLength == 0)
			target = 0;
		else if (target > (long long)mDataLength - 1)
			target = (long long)mDataLength - 1;
		mOffset = (unsigned int)target;
	}

	unsigned long long MemoryFile::pos()
	{
		return mOffset;
	}

	const unsigned char * MemoryFile::getMemPtr()
	{
		return mDataPtr;
	}

	MemoryFile::~MemoryFile()
	{
		if (mDataOwned)
			delete[] mDataPtr;
	}

	MemoryFile::MemoryFile()
	{
		mDataPtr = 0;
		mDataLength = 0;
		mOffset = 0;
		mDataOwned = false;
	}

	result MemoryFile::openMem(const unsigned char *aData, unsigned int aDataLength, bool aCopy, bool aTakeOwnership)
	{
		if (aData == NULL || aDataLength == 0)
			return INVALID_PARAMETER;

		if (mDataOwned)
			delete[] mDataPtr;
		mDataPtr = 0;
		mOffset = 0;

		mDataLength = aDataLength;

		if (aCopy)
		{
			mDataOwned = true;
			mDataPtr = new unsigned char[aDataLength];
			if (mDataPtr == NULL)
				return OUT_OF_MEMORY;
			memcpy((void *)mDataPtr, aData, aDataLength);
			return SO_NO_ERROR;
		}

		mDataPtr = aData;
		mDataOwned = aTakeOwnership;
		return SO_NO_ERROR;
	}

	result MemoryFile::openToMem(const char *aFile)
	{
		if (!aFile)
			return INVALID_PARAMETER;
		if (mDataOwned)
			delete[] mDataPtr;
		mDataPtr = 0;
		mOffset = 0;

		DiskFile df;
		int res = df.open(aFile);
		if (res != SO_NO_ERROR)
			return res;

		// In-memory data keeps a 32-bit length; a file beyond it can only be streamed.
		unsigned long long length = df.length();
		if (length > 0xFFFFFFFFull)
			return OUT_OF_MEMORY;
		mDataLength = (unsigned int)length;
		mDataPtr = new unsigned char[mDataLength];
		if (mDataPtr == NULL)
			return OUT_OF_MEMORY;
		df.read((unsigned char*)mDataPtr, mDataLength);
		mDataOwned = true;
		return SO_NO_ERROR;
	}

	result MemoryFile::openFileToMem(File *aFile)
	{
		if (!aFile)
			return INVALID_PARAMETER;
		if (mDataOwned)
			delete[] mDataPtr;
		mDataPtr = 0;
		mOffset = 0;

		unsigned long long length = aFile->length();
		if (length > 0xFFFFFFFFull)
			return OUT_OF_MEMORY;
		mDataLength = (unsigned int)length;
		mDataPtr = new unsigned char[mDataLength];
		if (mDataPtr == NULL)
			return OUT_OF_MEMORY;
		aFile->read((unsigned char*)mDataPtr, mDataLength);
		mDataOwned = true;
		return SO_NO_ERROR;
	}

	int MemoryFile::eof()
	{
		if (mOffset >= mDataLength)
			return 1;
		return 0;
	}
}

extern "C"
{
	int Soloud_Filehack_fgetc(Soloud_Filehack *f)
	{
		SoLoud::File *fp = (SoLoud::File *)f;
		if (fp->eof())
			return EOF;
		return fp->read8();
	}

	int Soloud_Filehack_fread(void *dst, int s, int c, Soloud_Filehack *f)
	{
		SoLoud::File *fp = (SoLoud::File *)f;
		return fp->read((unsigned char*)dst, s*c) / s;

	}

	int Soloud_Filehack_fseek(Soloud_Filehack *f, int idx, int base)
	{
		SoLoud::File *fp = (SoLoud::File *)f;
		switch (base)
		{
		case SEEK_CUR:
			fp->seek((long long)fp->pos() + idx);
			break;
		case SEEK_END:
			fp->seek((long long)fp->length() + idx);
			break;
		default:
			fp->seek(idx);
		}
		return 0;
	}

	int Soloud_Filehack_ftell(Soloud_Filehack *f)
	{
		SoLoud::File *fp = (SoLoud::File *)f;
		return (int)fp->pos();
	}

	int Soloud_Filehack_fclose(Soloud_Filehack *f)
	{
		SoLoud::File *fp = (SoLoud::File *)f;
		delete fp;
		return 0;
	}

	Soloud_Filehack * Soloud_Filehack_fopen(const char *aFilename, char * /*aMode*/)
	{
		SoLoud::DiskFile *df = new SoLoud::DiskFile();
		int res = df->open(aFilename);
		if (res != SoLoud::SO_NO_ERROR)
		{
			delete df;
			df = 0;
		}
		return (Soloud_Filehack*)df;
	}

	int Soloud_Filehack_fopen_s(Soloud_Filehack** f, const char* aFilename, char* /*aMode*/)
	{
		*f = Soloud_Filehack_fopen(aFilename, 0);
		return 1;
	}
}
