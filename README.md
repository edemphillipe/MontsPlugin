# Guitar Studio Pro

Estúdio de guitarra **standalone** (não precisa de DAW) para **macOS** e **Windows**, com uma única base de código em C++.

## Status (Fase 1: núcleo tocável)

| Recurso | Status | Como foi verificado |
| --- | --- | --- |
| Áudio: Core Audio (Mac), WASAPI e ASIO (Windows), escolha de interface, entrada, sample rate e buffer | Pronto | Compila; precisa do seu teste com interface real |
| Leitura de capturas **NAM** (.nam): WaveNet, LSTM, A2/Slimmable | Pronto | Saída idêntica (diferença 0) à biblioteca oficial NeuralAmpModelerCore |
| Leitura de capturas **AIDA-X** (.json): LSTM/GRU, modelos com 1 ou 2 knobs | Pronto | Igual ao RTNeural (2.6e-7) e a uma reimplementação independente em numpy (`tools/verify_aidax.py`, 2.4e-7) |
| Conversão de taxa (ex.: interface em 44,1 kHz com captura de 48 kHz) | Pronto | Ida e volta com SNR de 92,7 dB, ganho 0,000 dB |
| Troca de captura sem parar o áudio (fade de 15 ms) | Pronto | Teste automático sem cliques nem NaN |
| Normalização de volume das capturas NAM (-18 dB, mesmo padrão do plugin NAM) | Pronto | |
| IR de gabinete (WAV/AIFF) | Pronto | Usa a convolução do JUCE; precisa do seu teste de ouvido |
| Noise gate, EQ de 3 bandas (64 bits), ganho de entrada e saída, medidores | Pronto | Testes do gate; EQ precisa de teste de ouvido |
| Biblioteca: pasta, organização por fabricante/modelo (subpastas), busca, favoritos, anterior/próxima | Pronto | |
| Lembrar configurações (interface, captura, IR, knobs, favoritos) | Pronto | |
| Pedaleira com 11 pedais: compressor, boost, overdrive, distortion, fuzz, chorus, flanger, phaser, tremolo, delay, reverb | Pronto | 15 testes automáticos (ganho, aliasing com oversampling 8x, estabilidade com feedback máximo, ausência de NaN/cliques); precisa do seu teste de ouvido |
| Delay estéreo: Digital, Analógico, Fita, Reverso, Ping-pong, Modulado | Pronto | Tempo do eco medido em teste (erro < 1 amostra) |
| Reverb estéreo: Hall, Plate, Room, Spring, Ambient, Cloud, Shimmer | Pronto | Cauda decai e é estável em todos os modos (teste) |
| Tap tempo (botão TAP ou tecla T) e subdivisões (1/4, 1/8, pontuadas, tercinas) | Pronto | Teste do cálculo de BPM e do reset após pausa |
| Arrastar pedais para mudar a ordem, inclusive antes/depois do amp | Pronto | Testado na interface; ordem salva e restaurada |
| Uso de CPU com todos os pedais ligados | Medido | 4,7% de um núcleo (48 kHz, buffer 128) |
| Amplificadores próprios (Gain/Volume/Bass/Mid/Treble/Presence/Master), gabinete e microfone | Fase 2 (próximo PR) | Ainda não existe |
| Gravação, mixer, reamp, música de referência | Fase 3 | |
| Projetos/presets, instaladores assinados | Fase 4 | |

## Baixar e instalar

Cada alteração no código gera automaticamente as versões para Mac e Windows. Veja [docs/INSTALACAO.md](docs/INSTALACAO.md).

## Compilar no MacBook

```bash
xcode-select --install          # ferramentas da Apple (uma vez)
brew install cmake               # se ainda não tiver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
ctest --test-dir build -C Release --output-on-failure
open "build/src/app/GuitarStudioPro_artefacts/Release/Guitar Studio Pro.app"
```

A primeira configuração baixa JUCE, NeuralAmpModelerCore e RTNeural (alguns minutos).

## Estrutura

```
src/core/      motor compartilhado Mac/Windows (sem código específico de plataforma)
  captures/    leitura de NAM e AIDA-X
  dsp/         gate, EQ, conversor de taxa
  engine/      cadeia de sinal e troca de capturas em tempo real
src/app/       aplicativo (interface JUCE)
tests/         testes automáticos (rodam no Mac e no Windows a cada build)
tools/         verificações independentes
docs/          instalação, arquitetura e licenças
```

Mais detalhes em [docs/ARQUITETURA.md](docs/ARQUITETURA.md) e [docs/LICENCAS.md](docs/LICENCAS.md).
