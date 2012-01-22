/////////////////////////////////////////////////////////////////////////////////////////////////////////
// MaratisCommon
// MPackageManagerNPK.cpp
/////////////////////////////////////////////////////////////////////////////////////////////////////////

//========================================================================
// Copyright (c) 2012 Philipp Geyer <http://nistur.com>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================

#include <MEngine.h>
#include "MPackageManagerNPK.h"

#include <stdio.h>
#include <npk.h>
#include <npk_dev.h>

#define M_MAX_PACKAGES 1

int teakey[4] = { 0,0,0,0 };

class MPackageFile : public MFile
{
private:
	
	char* m_buffer;
	size_t m_size;
	char* m_pos;
	
public:
	
	MPackageFile(MPackageEnt ent)
	{
		m_size = npk_entity_get_size(ent);
		m_buffer = new char[m_size];
		npk_entity_read(ent, m_buffer);
		m_pos = m_buffer;
	}
	
	static MPackageFile * getNew(MPackageEnt ent)
	{
		return new MPackageFile(ent);
	}

	void open(const char* path, const char* mode)
	{
		// look up the new entity
		while(0);
	}

	int close()
	{
		SAFE_DELETE_ARRAY(m_buffer);
		return 0;
	}
	
	void destroy(void){ delete this; }

	size_t read(void* dest, size_t size, size_t count)
	{
		int copySize = MIN(size*count, m_size - tell());
		memcpy(dest, m_pos, copySize);
		m_pos += copySize;

		return copySize;
	}

	// unimplemented as I don't think we want writing to package files like this
	size_t write(const void* str, size_t size, size_t count) { return 0; }
	int print(const char* format, ...) { return 0; }
	int print(const char* format, va_list args) { return 0; }
	
	int seek(long offset, int whence)
	{
		switch(whence)
		{
		case SEEK_SET:
			m_pos = m_buffer + offset;
			break;
		case SEEK_CUR:
			m_pos += offset;
			break;
		case SEEK_END:
			m_pos = m_buffer + m_size - offset;
			break;
		default:
			return 1;
		}

		return 0;
	}

	long tell()
	{
		return m_pos - m_buffer;
	}

	void rewind()
	{
		m_pos = m_buffer;
	}
	
	// We can only be constructed if we have a Package Ent, so we're always true, but just check size to be sure
	bool isOpen() { return m_size> 0; }
};


MFile* MPackageFileOpenHook::open(const char* path, const char* mode)
{
	MEngine* engine = MEngine::getInstance();
	MSystemContext * system = engine->getSystemContext();
	
	char localFilename[256];
	getLocalFilename(localFilename, system->getWorkingDirectory(), path);
	
	if(MPackageEnt ent = engine->getPackageManager()->findEntity(localFilename))
		return MPackageFile::getNew(ent);
	
	return MStdFile::getNew(path, mode);
}


struct MPackageNPK {
	NPK_PACKAGE package;
	MString		filename;
};

MPackageManagerNPK::MPackageManagerNPK():
m_packages(NULL),
m_fileOpenHook(NULL)
{
}

MPackageManagerNPK::~MPackageManagerNPK()
{
	cleanup();
}

void MPackageManagerNPK::init()
{
	if(! m_fileOpenHook)
	{
		m_fileOpenHook = new MPackageFileOpenHook;
		M_registerFileOpenHook(m_fileOpenHook);
		
		MPackageNPK** packages = new MPackageNPK*[M_MAX_PACKAGES];
		m_packages = (MPackage*)packages;
		for(int i = 0; i < M_MAX_PACKAGES; ++i)
			m_packages[i] = 0;
	}
}

void MPackageManagerNPK::cleanup()
{
	if(m_fileOpenHook)
	{
		if(M_getFileOpenHook() == m_fileOpenHook)
			M_registerFileOpenHook(0);
		SAFE_DELETE(m_fileOpenHook);
	}
	
	// unsafe
	//if(M_getFileOpenHook())
	//	delete M_getFileOpenHook();
	//M_registerFileOpenHook(0);
	
	if(m_packages)
	{
		for(int i = 0; i < M_MAX_PACKAGES; ++i)
		{
			if(m_packages[i] != 0)
			{
				MPackageNPK* pack = (MPackageNPK*)m_packages[i];
				npk_package_close(pack->package);
				
				delete pack;
				m_packages[i] = 0;
			}
		}
		
		SAFE_DELETE_ARRAY(m_packages);
		//delete [] m_packages;
	}
}

MPackage MPackageManagerNPK::loadPackage(const char* packageName)
{
	MPackageNPK* pack = new MPackageNPK;

	if(!(pack->package = npk_package_open(packageName, teakey)))
	{
		delete pack;
		return 0;
	}
	pack->filename = packageName;

	if(!mountPackage(pack))
	{
		npk_package_close(pack->package);
		delete pack;
		pack = 0;
	}

	return pack;
}

MPackageEnt MPackageManagerNPK::findEntity(const char* name)
{
	// we probably actually want to loop backwards through these
	// so we can override defaults with extra packages
	for(int i = 0; i < M_MAX_PACKAGES; ++i)
	{
		if(m_packages[i])
		{
			MPackageNPK* pack = (MPackageNPK*)m_packages[i];
			if(NPK_ENTITY ent = npk_package_get_entity(pack->package, name))
			{
				return ent;
			}
		}
	}

	return 0;
}

void MPackageManagerNPK::unloadPackage(MPackage package)
{
	for(int i = 0; i < M_MAX_PACKAGES; ++i)
	{
		if(m_packages[i] == package)
		{
			m_packages[i] = 0;
			MPackageNPK* pack = (MPackageNPK*)package;
			npk_package_close(pack->package);

			delete pack;
			break;
		}
	}
}

void MPackageManagerNPK::offlinePackage(MPackage package)
{
	unloadPackage(package);
}

MPackage MPackageManagerNPK::openPackage(const char* packageName)
{
#ifdef M_PACKAGE_WRITABLE
	MPackageNPK* package = new MPackageNPK;
	if(isFileExist(packageName))
	{
		package->package = npk_package_open(packageName, teakey);
	}
	else
	{
		npk_package_alloc(&package->package, teakey);
	}

	if(package->package)
	{
		package->filename = packageName;
		return package;
	}
	delete package;
#endif
	return 0;
}

void MPackageManagerNPK::closePackage(MPackage package)
{
#ifdef M_PACKAGE_WRITABLE
	if(package)
	{
		MPackageNPK* pack = (MPackageNPK*)package;
		npk_package_save(pack->package, pack->filename.getData(), true);
		npk_package_close(pack->package);
		delete package;
	}
#endif
}

MPackageEnt MPackageManagerNPK::addFileToPackage(const char* filename, MPackage package)
{
#ifdef M_PACKAGE_WRITABLE
	NPK_ENTITY entity = 0;
	if(filename && package)
	{
		MEngine* engine = MEngine::getInstance();
		MSystemContext * system = engine->getSystemContext();

		char localFilename[256];
		getLocalFilename(localFilename, system->getWorkingDirectory(), filename);
		npk_package_add_file(((MPackageNPK*)package)->package, filename, localFilename, &entity);
	}

	return entity;
#endif
	return 0;
}

MPackage MPackageManagerNPK::mountPackage(MPackage package)
{
	if(m_packages[M_MAX_PACKAGES- 1])
		return 0; // fail, no free packages

	// find the first empty package slot
	int pkgNum = 0;
	for(pkgNum; pkgNum < M_MAX_PACKAGES; ++pkgNum)
		if(m_packages[pkgNum] == 0)
			break;

	MPackageNPK* pack = (MPackageNPK*)package;

	// loaded the package, now save it for later access
	m_packages[pkgNum] = pack;
	return pack;
}