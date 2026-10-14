#include "sep/ModelFile.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "sep/Half.h"

namespace {
constexpr char kMagic[8] = {'W', 'A', 'V', 'A', 'P', 'P', 'M', '1'};

size_t elementBytes(ModelFile::Type t) { return t == ModelFile::kF32 ? 4 : t == ModelFile::kF16 ? 2 : 1; }
}  // namespace

std::unique_ptr<ModelFile> ModelFile::open(const std::string &path, std::string &error) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error = "No se encontró el modelo";
        return nullptr;
    }
    struct stat st {};
    fstat(fd, &st);
    auto m = openFd(fd, 0, static_cast<int64_t>(st.st_size), error);
    ::close(fd);  // el mapeo sigue válido
    return m;
}

std::unique_ptr<ModelFile> ModelFile::openFd(int fd, int64_t offset, int64_t length, std::string &error) {
    if (fd < 0 || offset < 0 || length < 64) {
        error = "El modelo no es válido";
        return nullptr;
    }
    const int64_t page = sysconf(_SC_PAGESIZE);
    const int64_t start = offset - offset % page;
    const size_t bytes = static_cast<size_t>(length + (offset - start));
    void *map = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, fd, static_cast<off_t>(start));
    if (map == MAP_FAILED) {
        error = "No hay memoria para abrir el modelo";
        return nullptr;
    }
    std::unique_ptr<ModelFile> m(new ModelFile());
    m->mMap = map;
    m->mMapBytes = bytes;
    m->mBase = static_cast<const uint8_t *>(map) + (offset - start);
    m->mBytes = static_cast<size_t>(length);
    if (!m->parse(error)) return nullptr;
    return m;
}

ModelFile::~ModelFile() {
    if (mMap) munmap(mMap, mMapBytes);
}

bool ModelFile::parse(std::string &error) {
    error = "El modelo no es válido";
    if (std::memcmp(mBase, kMagic, sizeof kMagic) != 0) return false;
    uint32_t len = 0;
    std::memcpy(&len, mBase + 8, 4);
    if (12 + static_cast<size_t>(len) > mBytes) return false;
    std::string jsonError;
    mHeader = Json::parse(std::string(reinterpret_cast<const char *>(mBase + 12), len), jsonError);
    if (!mHeader.isObject()) return false;
    size_t dataStart = 12 + len;
    dataStart += (64 - dataStart % 64) % 64;
    const Json &table = mHeader["tensores"];
    for (size_t i = 0; i < table.size(); ++i) {
        const Json &t = table.valueAt(i);
        Tensor x;
        const std::string type = t["tipo"].asString();
        if (type == "f32") x.type = kF32;
        else if (type == "f16") x.type = kF16;
        else if (type == "u8") x.type = kU8;
        else return false;
        x.count = 1;
        const Json &shape = t["forma"];
        for (size_t k = 0; k < shape.size(); ++k) {
            const int64_t d = shape[k].asInt(-1);
            if (d < 0 || d > (int64_t(1) << 31)) return false;
            x.shape.push_back(d);
            x.count *= d;
        }
        const int64_t off = t["desplazamiento"].asInt(-1);
        const int64_t bytes = t["bytes"].asInt(-1);
        if (off < 0 || bytes != x.count * static_cast<int64_t>(elementBytes(x.type))) return false;
        if (dataStart + static_cast<size_t>(off) + static_cast<size_t>(bytes) > mBytes) return false;
        x.data = mBase + dataStart + off;
        mTensors[table.keyAt(i)] = std::move(x);
    }
    error.clear();
    return true;
}

const ModelFile::Tensor *ModelFile::find(const std::string &name) const {
    auto it = mTensors.find(name);
    return it == mTensors.end() ? nullptr : &it->second;
}

void ModelFile::toFloat(const Tensor &t, float *out) {
    const size_t n = static_cast<size_t>(t.count);
    switch (t.type) {
        case kF32:
            std::memcpy(out, t.data, n * sizeof(float));
            break;
        case kF16: {
            // Los datos están alineados a 64 bytes desde el inicio del modelo, pero el modelo
            // puede empezar en cualquier byte dentro del APK: se copia por partes.
            uint16_t tmp[1024];
            for (size_t i = 0; i < n; i += 1024) {
                const size_t k = n - i < 1024 ? n - i : 1024;
                std::memcpy(tmp, t.data + 2 * i, 2 * k);
                halfToFloat(tmp, out + i, k);
            }
            break;
        }
        case kU8:
            for (size_t i = 0; i < n; ++i) out[i] = t.data[i];
            break;
    }
}

bool ModelFile::floats(const std::string &name, std::vector<float> &out, int64_t count) const {
    const Tensor *t = find(name);
    if (!t || (count >= 0 && t->count != count)) return false;
    out.resize(static_cast<size_t>(t->count));
    toFloat(*t, out.data());
    return true;
}
