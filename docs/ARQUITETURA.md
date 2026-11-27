# Arquitetura

- **Linguagem:** C++20. **Framework:** JUCE 9 (áudio, interface, convolução).
- **Capturas:** NeuralAmpModelerCore (NAM, oficial) e RTNeural (mesma biblioteca do AIDA-X).
- **Build:** CMake. macOS gera binário universal (arm64 + x86_64); Windows x64 com MSVC. GitHub Actions compila e testa os dois a cada push.

## Cadeia de sinal (Fase 1)

```
entrada (canal escolhido) → ganho → noise gate → captura NAM/AIDA-X → IR de gabinete → EQ 3 bandas → volume → saídas L/R
```

## Tempo real

- A thread de áudio não aloca memória e não usa travas.
- Capturas são carregadas e preparadas na thread da interface, entregues por ponteiro atômico,
  entram com fade de 15 ms e a anterior é liberada depois, fora da thread de áudio.
- Se a captura foi treinada em outra taxa (ex.: 48 kHz) e a interface roda em 44,1 kHz,
  o áudio é convertido para a taxa da captura e de volta (sinc com janela Kaiser, 32 taps),
  com latência extra de cerca de 40 amostras.
- NAM e RTNeural ficam em bibliotecas separadas para que suas cópias de Eigen/nlohmann não se misturem.
