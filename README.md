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
| Pedaleira com 12 pedais (o distortion CRUNCH tem GAIN, BASS, MIDDLE, TREBLE e LEVEL, no estilo amp britânico): compressor, boost, overdrive, distortion, fuzz, chorus, flanger, phaser, tremolo, delay, dual delay, reverb | Pronto | 15 testes automáticos (knob de ganho aumenta a distorção, aliasing abaixo de -30 dB, bypass transparente, troca liga/desliga sem clique, sem NaN); precisa do seu teste de ouvido |
| Delay estéreo: Digital, Analógico, Fita, Reverso, Ping-pong, Modulado | Pronto | Posição do eco medida em teste, livre e sincronizado ao TAP (erro até 3 amostras) |
| Dual delay (GEMINI): dois delays independentes, L e R, cada um com tempo (ms ou TAP com subdivisão própria), tipo (Digital, Analógico, Fita), repeats, tone e level; MOD, CROSS (repetições cruzadas entre os lados) e LINK (REPEATS e LEVEL dos dois lados juntos) | Pronto | Testes: posição do eco de cada lado, livre e no TAP (erro até 3 amostras); lados isolados com CROSS em 0 e cruzados em 1; tone de cada lado; estável no máximo em todas as 9 combinações de tipos |
| Reverb estéreo: Hall, Plate, Room, Spring, Ambient, Cloud, Shimmer, com HI DAMP, LOW DAMP e TONE | Pronto | T60 medido acompanha o knob Decay, cauda estéreo descorrelacionada, Shimmer estável. Na cauda do Cloud: HI DAMP tira 39 dB em 5 kHz, LOW DAMP tira 38 dB em 120 Hz, TONE inclina 16 dB (testes) |
| Tap tempo (botão TAP ou tecla T) e subdivisões (1/4, 1/8, pontuadas, tercinas) | Pronto | Teste do cálculo de BPM e do reset após pausa |
| Arrastar pedais para mudar a ordem, inclusive antes/depois do amp | Pronto | Mudar, salvar e restaurar a ordem: teste automático. A pedaleira foi conferida em captura de tela; o arrastar com o mouse precisa do seu teste |
| Uso de CPU com todos os pedais ligados | Medido | 4,9% de um núcleo nesta máquina de testes (48 kHz, buffer 64) |
| Uso de CPU do amp mais pesado + gabinete | Medido | 5,5% de um núcleo nesta máquina de testes (48 kHz, buffer 64) |
| 6 amplificadores próprios (Americano Limpo, Brit Crunch, Chime Classe A, Brit Espesso, Lead Hot-Rod, Moderno Alto Ganho) com GAIN, VOLUME, BASS, MIDDLE, TREBLE, PRESENCE, MASTER | Pronto | Tone stack: equações do circuito conferidas com uma solução numérica independente (diferença 0,0001 dB) e o filtro digital com as equações (0,05 dB). GAIN aumenta a distorção em todos; volumes calibrados para -18 dBFS RMS com knobs em 5. Precisa do seu teste de ouvido |
| Simulação de gabinete (5 caixas) e microfone (dinâmico, fita, condensador) com posição e distância | Pronto | Modelo por filtros (não é IR medido). Testes: corte de agudos do alto-falante, posição escurece o som na borda, distância reduz o grave de proximidade. Para um gabinete real específico, use um IR |
| Troca de amp/gabinete sem estalo | Pronto | Teste automático trocando o amp com o som tocando |
| Visual realista: pedais com carcaça, knobs, footswitch, LED e cabos; amp como cabeçote; gabinete com tela | Pronto | Conferido em capturas de tela |
| Clicar no pedal abre um editor com todos os controles (tipo, subdivisão, knobs) | Pronto | Conferido no app; precisa do seu teste com o mouse |
| Presets de pedaleira: salvar, carregar e excluir a pedaleira inteira com amp, captura, IR e ajustes, em Documentos/Guitar Studio Pro/Pedaleiras | Pronto | Testes de salvar, ler, listar e excluir; salvar pela tela conferido (arquivo gerado com todos os ajustes) |
| Afinador cromático (corta a saída enquanto está ligado) | Pronto | Testes com as 6 cordas (erro máximo 0,13 cent), cordas desafinadas (dentro de 1 cent), silêncio e ruído não mostram nota, saída mutada |
| Gravação multipista (até 8 faixas): cada take guarda a guitarra limpa (DI) e o som com amp e pedais, em WAV 24 bits | Pronto | Testes: DI igual à entrada, som igual à saída do amp, nenhuma amostra perdida. Precisa do seu teste com interface real |
| Compensação de latência: a faixa gravada fica alinhada com o que você ouvia | Pronto | Teste com latência de 100 amostras: o take toca exatamente na amostra certa. Usa a latência que o driver informa |
| Mixer por faixa: volume, pan, mudo, solo; armar faixa para regravar por cima (o take antigo continua salvo) | Pronto | Testes de volume (-6 dB), pan, solo, mudo e regravação |
| Reamp: a gravação limpa passa pelo amp e pedais atuais e vira o novo som da faixa (com 3 s a mais para a cauda de delay e reverb) | Pronto | Teste: novo som na posição exata, sem vazar a guitarra ao vivo; reamp cancelado no meio não altera a faixa |
| Música de referência para tocar junto (WAV, AIFF, FLAC, MP3, OGG), com volume e mudo | Pronto | Teste com arquivo em 44,1 kHz tocando numa sessão de 48 kHz: posição certa (erro de até 2 amostras) |
| Metrônomo no BPM do TAP (só no fone, não entra no mix) | Pronto | Teste: clique em cada tempo, tempo 1 mais forte, fora do mix exportado |
| Exportar mix em WAV estéreo 24 bits | Pronto | Teste comparando o arquivo com a reprodução; exportado também pela tela |
| Projetos de gravação em Documentos/Guitar Studio Pro/Gravações (abre o último ao iniciar) | Pronto | Teste: fechar e reabrir o projeto mantém as faixas |
| Tela de gravação: formas de onda, régua de tempo, clique para posicionar, barra de espaço toca/pausa, R grava | Pronto | Conferida em capturas de tela com um projeto de exemplo. Tocar e gravar com áudio de verdade precisa do seu teste (esta máquina não tem interface de áudio) |
| Projetos de gravação, instaladores assinados | Fase 4 | |

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
