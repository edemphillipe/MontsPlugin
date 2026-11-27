# Arquitetura

- **Linguagem:** C++20. **Framework:** JUCE 9 (áudio, interface, convolução).
- **Capturas:** NeuralAmpModelerCore (NAM, oficial) e RTNeural (mesma biblioteca do AIDA-X).
- **Build:** CMake. macOS gera binário universal (arm64 + x86_64); Windows x64 com MSVC. GitHub Actions compila e testa os dois a cada push.

## Cadeia de sinal (Fases 1 e 2)

```
entrada (canal escolhido) → ganho → noise gate → [pedais] → AMP → [pedais] → volume → saídas L/R

AMP = (captura NAM/AIDA-X  ou  amplificador interno) → gabinete (IR de arquivo ou simulado) → EQ 3 bandas
```

O bloco AMP pode ficar em qualquer posição da pedaleira; depois dele o sinal é estéreo.

## Amplificadores internos (`src/core/amp/`)

Os 6 modelos usam a mesma estrutura, com valores diferentes ("voicings"). Os nomes são
próprios; os sons são inspirados em famílias clássicas (amps americanos limpos, britânicos
crunch, classe A, britânicos de saturação grossa, hot-rod de lead e alto ganho moderno).

1. Entrada: filtro passa-altas (aperta os graves) e "bright" que diminui conforme o GAIN sobe.
2. Pré-amplificador: 2 a 5 estágios de válvula com saturação assimétrica (gera harmônicos
   pares), filtros de acoplamento e de capacitância Miller entre estágios. O GAIN fica depois
   do primeiro estágio, como nos amps reais.
3. Tone stack passivo (BASS/MIDDLE/TREBLE): função de transferência de 3ª ordem derivada das
   equações do circuito, com valores de componentes típicos de cada família, discretizada
   pela transformada bilinear em 64 bits.
4. Amplificador de potência: PRESENCE e ressonância, saturação push-pull (ou classe A),
   "sag" da fonte e MASTER no final.
5. A parte não linear roda com oversampling 4x para evitar aliasing.

O gabinete simulado (`CabinetSim`) modela ressonância do alto-falante, cor da caixa, picos de
quebra do cone e o corte de agudos, mais a curva do microfone, a posição (centro → borda) e a
distância (menos grave de proximidade e a primeira reflexão do chão). É um modelo por filtros:
para reproduzir um gabinete real específico, carregue um IR.

Trocar de amp, de gabinete ou de captura para amp faz um fade de 10 ms para não estalar.

## Tempo real

- A thread de áudio não aloca memória e não usa travas.
- Capturas são carregadas e preparadas na thread da interface, entregues por ponteiro atômico,
  entram com fade de 15 ms e a anterior é liberada depois, fora da thread de áudio.
- Se a captura foi treinada em outra taxa (ex.: 48 kHz) e a interface roda em 44,1 kHz,
  o áudio é convertido para a taxa da captura e de volta (sinc com janela Kaiser, 32 taps),
  com latência extra de cerca de 40 amostras.
- NAM e RTNeural ficam em bibliotecas separadas para que suas cópias de Eigen/nlohmann não se misturem.
