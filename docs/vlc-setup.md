# Configurando o VLC

## Ativar a interface HTTP

1. Abra o VLC no computador que vai tocar o video.
2. Acesse `Ferramentas > Preferencias`.
3. No canto inferior esquerdo, selecione `Tudo`.
4. Abra `Interface > Interfaces principais`.
5. Marque `Web`.
6. Abra `Interface > Interfaces principais > Lua`.
7. Em `Lua HTTP`, defina uma senha.
8. Salve e reinicie o VLC.

Tambem e possivel iniciar pelo terminal:

```bash
vlc --extraintf http --http-password sua_senha
```

Se o VLC foi instalado via Snap e falhar ao abrir com erro parecido com `failed to load driver: iris`, use o script do projeto:

```bash
VLC_FORCE_SOFTWARE_GL=1 VLC_PASSWORD=sua_senha ./scripts/start-vlc.sh
```

Neste computador o Snap do VLC mostrou erro de OpenGL com a GPU Intel `8086:46a6`. `VLC_FORCE_SOFTWARE_GL=1` aplica `LIBGL_ALWAYS_SOFTWARE=1`, que evita essa falha. A alternativa mais estavel e instalar o VLC pelo APT em vez do Snap.

## Rodar o controle web

Na pasta do projeto:

```bash
cp .env.example .env
npm start
```

O terminal vai mostrar algo como:

```text
VLC Control ouvindo em http://localhost:3000
Rede local: http://192.168.0.10:3000
Destino VLC: http://127.0.0.1:8080
```

Abra o endereco de rede local no Safari do iPhone.

## Abrir filmes com o botao direito

Rode uma vez:

```bash
./scripts/install-open-with-vlc.sh
```

Depois disso, ao usar `Abrir com VLC` no gerenciador de arquivos, o VLC abre pelo lancador local do projeto e ja inicia com a interface HTTP ativa. O servidor do controle web pode continuar sendo iniciado no terminal com `npm start`.

## Quando o iPhone nao conecta

- Confirme que iPhone e computador estao na mesma rede Wi-Fi.
- Libere a porta `3000` no firewall do computador.
- Confira se o VLC esta aberto e com a interface Web ativa.
- Confirme se `VLC_PASSWORD` e igual a senha configurada em `Lua HTTP`.
- Se o VLC estiver em outro computador, rode com `VLC_HOST=IP_DO_COMPUTADOR_DO_VLC`.

Exemplo:

```bash
VLC_HOST=192.168.0.20 VLC_PASSWORD=sua_senha npm start
```
