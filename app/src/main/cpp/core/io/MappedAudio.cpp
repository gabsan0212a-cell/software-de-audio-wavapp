#include "io/MappedAudio.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>

#include "io/WavReader.h"

std::unique_ptr<MappedAudio> MappedAudio::open(const std::string &path, std::string &error) {
    WavReader reader;
    error = reader.open(std::fopen(path.c_str(), "rb"));
    if (!error.empty()) return nullptr;
    if (!reader.isFloat() || reader.bits() != 32 || reader.channels() > 2 || (reader.dataOffset() & 3)) {
        error = "La pista no está en el formato interno";
        return nullptr;
    }

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error = "No se pudo abrir la pista";
        return nullptr;
    }
    struct stat st {};
    fstat(fd, &st);
    const size_t bytes = static_cast<size_t>(st.st_size);
    void *map = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);  // el mapeo sigue válido
    if (map == MAP_FAILED) {
        error = "No hay memoria para mapear la pista";
        return nullptr;
    }

    std::unique_ptr<MappedAudio> a(new MappedAudio());
    a->mMap = map;
    a->mMapBytes = bytes;
    a->mData = reinterpret_cast<const float *>(static_cast<const char *>(map) + reader.dataOffset());
    a->mChannels = reader.channels();
    a->mSampleRate = reader.sampleRate();
    a->mFrames = reader.frames();
    a->mPath = path;
    return a;
}

MappedAudio::~MappedAudio() {
    if (mMap) munmap(mMap, mMapBytes);
}

void MappedAudio::prefetch(int64_t frame, int64_t count) const {
    if (frame < 0) { count += frame; frame = 0; }
    if (frame >= mFrames || count <= 0) return;
    if (frame + count > mFrames) count = mFrames - frame;
    const long page = sysconf(_SC_PAGESIZE);
    const char *base = static_cast<const char *>(mMap);
    const char *start = reinterpret_cast<const char *>(mData + frame * mChannels);
    const char *end = reinterpret_cast<const char *>(mData + (frame + count) * mChannels);
    const uintptr_t s = reinterpret_cast<uintptr_t>(start) & ~static_cast<uintptr_t>(page - 1);
    const char *aligned = reinterpret_cast<const char *>(s);
    if (aligned < base) aligned = base;
    madvise(const_cast<char *>(aligned), static_cast<size_t>(end - aligned), MADV_WILLNEED);
}
