#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "sep/ModelFile.h"
#include "sep/Stft.h"
#include "sep/ThreadPool.h"

// Mel-Band RoFormer (separa la voz de la música), igual al código original de PyTorch
// (Kim / lucidrains, tools/modelos): STFT → bandas mel → transformers que alternan tiempo y
// frecuencia → una máscara compleja por banda → se promedian donde las bandas se enciman →
// espectro × máscara → STFT inversa. Una pasada procesa un trozo de `chunk()` muestras estéreo.
//
// Los pesos de los transformers y de la entrada de bandas se pasan a float32 al cargar; los de
// las máscaras (la mayor parte del modelo) se quedan en el archivo y se convierten banda por
// banda al usarlos, así la memoria no pasa de unos cientos de MB.
class MelRoformer {
public:
    struct Config {
        int dim = 0, depth = 0, bands = 0, heads = 0, dimHead = 0;
        int timeDepth = 1, freqDepth = 1, maskDepth = 1, mlpFactor = 4;
        int sampleRate = 44100, nfft = 2048, hop = 441;
        int chunk = 0, overlap = 2;
    };

    // Lo de adentro de una pasada (para las pruebas): [cuadro][banda][dim] y máscaras por cuadro.
    struct Inside {
        std::vector<float> bands;
        std::vector<std::vector<float>> time, freq;  // después de cada transformer
        std::vector<float> masks;                    // [cuadro][suma de entradas de las bandas]
        std::vector<std::pair<const char *, double>> times;  // segundos por etapa
    };

    static std::unique_ptr<MelRoformer> load(const ModelFile &file, std::string &error);

    const Config &config() const { return mCfg; }
    int chunk() const { return mCfg.chunk; }
    int frames() const { return mFrames; }

    // Una pasada sobre chunk() muestras por canal. Devuelve false si se canceló.
    // `inside` (opcional) se llena con lo de adentro.
    bool run(const float *inL, const float *inR, float *outL, float *outR, ThreadPool &pool,
             const std::atomic<bool> *cancel = nullptr, Inside *inside = nullptr);

private:
    struct Linear {
        std::vector<float> w, b;  // w: [salida][entrada] como PyTorch
        int in = 0, out = 0;
    };
    struct Layer {  // atención + feed-forward
        std::vector<float> attnGamma, ffGamma;
        Linear qkv, gates, proj, ff1, ff2;
    };
    struct Transformer {
        std::vector<Layer> layers;
        std::vector<float> outGamma;
        std::vector<float> cosT, sinT;  // RoPE: [posición][dimHead / 2]
    };
    struct Band {
        std::vector<int> bins;  // índices (frecuencia * 2 + canal) en orden
        int dimIn = 0;          // bins * 2 (real, imaginario)
        std::vector<float> gamma;
        Linear proj;
        // Máscara: capas del MLP (pesos en el archivo; se convierten al usarlos).
        std::vector<const ModelFile::Tensor *> maskW;
        std::vector<std::vector<float>> maskB;
        size_t maskOffset = 0;  // dónde empieza en mMasks (por cuadro)
    };
    // Memoria de trabajo de cada hilo.
    struct Scratch {
        std::vector<float> x, xn, qkv, gates, att, scores, hidden, w[3], h1, h2, y;
    };

    MelRoformer() = default;
    bool loadLinear(const ModelFile &f, const std::string &name, int in, int out, bool bias, Linear &l,
                    std::string &error);
    void transformer(const Transformer &tr, int sequences, int length, int firstRow, int seqStride, int rowStride,
                     Scratch &s);
    void maskBand(int b, Scratch &s);

    Config mCfg;
    int mFrames = 0, mBins = 0, mInner = 0, mMaskTotal = 0;
    std::vector<Transformer> mTime, mFreq;
    std::vector<Band> mBands;
    std::vector<float> mBandsPerBin;  // cuántas bandas cubren cada (frecuencia, canal)
    std::unique_ptr<StereoStft> mStft;

    // Memoria de una pasada
    std::vector<float> mSpec;   // [cuadro][frecuencia][canal][re, im]
    std::vector<float> mX;      // [cuadro][banda][dim]
    std::vector<float> mMasks;  // por banda: [cuadro][entrada de la banda]
    std::vector<Scratch> mScratch;
};
