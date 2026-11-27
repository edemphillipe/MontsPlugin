# Instalação do Guitar Studio Pro (versão de teste)

## Onde baixar

1. Abra o repositório no GitHub e clique na aba **Actions**.
2. Clique na execução mais recente com o ícone verde de sucesso.
3. Em **Artifacts**, baixe:
   - **GuitarStudioPro-macOS** (arquivo .dmg dentro do .zip), ou
   - **GuitarStudioPro-Windows** (arquivo .zip).

## macOS

1. Abra o .dmg e arraste **Guitar Studio Pro** para **Aplicativos**.
2. Esta versão de teste ainda não é assinada pela Apple. Na primeira vez:
   clique com o botão direito no app → **Abrir** → **Abrir**.
   Se o macOS bloquear, vá em **Ajustes do Sistema → Privacidade e Segurança** e clique em **Abrir Mesmo Assim**.
3. Permita o acesso ao **Microfone** quando o macOS pedir (é assim que o app recebe o sinal da interface de áudio).

Funciona em Macs com Apple Silicon (M1 a M4 e posteriores) e Intel, macOS 11 ou mais novo.

## Windows

1. Extraia o .zip em uma pasta (por exemplo, `C:\Guitar Studio Pro`).
2. Abra **Guitar Studio Pro.exe**. Se aparecer o aviso do SmartScreen, clique em **Mais informações → Executar assim mesmo** (a versão de teste ainda não tem certificado).
3. Para a menor latência, use o driver **ASIO** da sua interface de áudio.

## Primeiro uso

1. Clique em **Configurar áudio**, escolha sua interface, a taxa (48 kHz recomendado) e o buffer (64 ou 128).
2. No seletor ao lado, escolha a **entrada** onde a guitarra está ligada.
3. Clique em **Pasta...** na biblioteca e escolha a pasta onde estão suas capturas `.nam` e AIDA-X `.json`.
   Organize em subpastas, por exemplo `Capturas/Marshall/JCM800/`, e o app mostra "Marshall / JCM800".
4. Dê duplo clique em uma captura para carregá-la. Clique na estrela para favoritar.
5. Em **Gabinete IR**, carregue um IR (.wav) se a captura for só do amplificador (sem gabinete).

Dica: capturas de amplificador "full rig" já incluem o gabinete; nesse caso desligue o IR (LED verde do painel).
