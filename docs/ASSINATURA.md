# Assinatura dos instaladores (para quando for vender)

Hoje os instaladores saem **sem assinatura**: funcionam, mas o macOS pede "Abrir mesmo assim"
e o Windows mostra o aviso do SmartScreen. O CI já está pronto para assinar: basta cadastrar
os segredos abaixo no GitHub (**Settings → Secrets and variables → Actions → New repository secret**).
Sem eles, nada muda; com eles, o próximo build já sai assinado. Ninguém além de você precisa ver
os certificados: eles ficam só nos segredos do repositório.

## macOS (Apple Developer Program, US$ 99/ano)

1. Entre no Apple Developer Program com sua Apple ID.
2. Em **Certificates**, crie um certificado **Developer ID Application**, instale no Keychain do seu Mac
   e exporte como `.p12` com uma senha.
3. Em appleid.apple.com crie uma **senha de app** (para a notarização).
4. Cadastre os segredos:

| Segredo | O que é |
|---|---|
| `MACOS_CERT_P12` | o arquivo .p12 em base64 (`base64 -i certificado.p12 \| pbcopy`) |
| `MACOS_CERT_PASSWORD` | a senha do .p12 |
| `MACOS_SIGN_IDENTITY` | o nome do certificado, ex.: `Developer ID Application: Seu Nome (ABCDE12345)` |
| `APPLE_ID` | o e-mail da sua Apple ID |
| `APPLE_TEAM_ID` | o Team ID (10 caracteres, aparece na conta de desenvolvedor) |
| `APPLE_APP_PASSWORD` | a senha de app criada no passo 3 |

Com isso o CI assina o app (hardened runtime, permissão de entrada de áudio), assina o .dmg,
envia para a Apple notarizar e "grampeia" o resultado: o app abre direto, sem aviso.

## Windows (certificado de assinatura de código)

1. Compre um certificado de **assinatura de código** (OV ou EV) de uma autoridade certificadora.
   Hoje muitos vêm em token físico ou em nuvem; para o CI, o formato mais simples é um `.pfx`
   (alguns fornecedores, como o Azure Trusted Signing, usam outro método; se for o caso, ajustamos o CI).
2. Cadastre os segredos:

| Segredo | O que é |
|---|---|
| `WINDOWS_CERT_PFX` | o arquivo .pfx em base64 (`[Convert]::ToBase64String([IO.File]::ReadAllBytes("cert.pfx"))`) |
| `WINDOWS_CERT_PASSWORD` | a senha do .pfx |

O CI assina o programa, o instalador e o desinstalador. O aviso do SmartScreen some aos poucos,
conforme o certificado ganha reputação (com certificado EV, some na hora).

## Antes de vender

Além da assinatura, a venda precisa da **licença comercial do JUCE** (o framework do app).
