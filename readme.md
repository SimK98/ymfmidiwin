# ymfmidi for Windows
**Software OPL FM MIDI Synthesizer**

<img src="img/ymfmidiwin.png" width="96">

[紹介ページ（日本語）](https://simk98.github.io/np21w/ymfmidiwin.html)/  [Project Page (English)](https://simk98.github.io/np21w/ymfmidiwin-en.html)  

[ymfmidi for Windows ダウンロード](https://github.com/SimK98/ymfmidiwin/releases)

**ヤマハのFM音源をエミュレーションする [ymfm](https://github.com/aaronsgiles/ymfm) を MIDI プレーヤーとして実装した 
[ymfmidi](https://github.com/devinacker/ymfmidi) を、さらに Windows へ移植したものです。**

ここで言うFM音源のMIDIとは、昔のSound Blasterなどに載っていたOPL3チップを使ったMIDIのことです。
今時はサウンドフォントを使った高品質MIDI音源が手軽に使用できますので、FM音源のMIDIは品質面で到底太刀打ちできるものではありません。

しかし、PCにFM音源が搭載されなくなって久しい今となっては、逆にFM音源によるMIDIの音色は新しい存在です。
このFM音源を再現したサウンドフォントは世の中にありますが、FM音源はサンプリング音源とは根本的に音の作り方が違います。
つまり「あの時のあの音」を目指すには、サウンドフォントではなくFM音源の波形合成の仕組みそのものの再現が（おそらく）ベストです。

そういうわけで、本ソフトウェアは**ヤマハのOPL系チップのFM音源をエミュレーションし、WindowsのMIDI環境（midiIn/midiOut API）からFM音源を利用可能にすること**を目的としています。


---

## 特徴

- **FM音源風サウンドフォントではなく、完全なFM音源エミュレーション**
  - YAMAHA OPL 系チップ（種類選択可能）を再現
  - 波形合成レベルでのエミュレーション
  - チップの複数使用によって同時発音数を増加可能

- **過去の Windows FM MIDI よりも MIDI 仕様への互換性が向上**
  - GM / GS / XG の処理に対応  
    （音色定義を調整することで、さらに互換性を向上可能）
  - 過去の Windows FM MIDI で正しく処理されていなかった仕様を修正  
    （ピッチベンド周りなど）

- **Windows ネイティブな MIDI デバイスとして動作**
  - MIDI IN の入力を受け取れる常駐型ソフトウェアシンセサイザー
  - loopMIDI 等の仮想 MIDI ケーブルを使用することで、  
    Windows 標準の midiOut API 対応アプリケーションから利用可能
  - MIDI メッセージがないときは自動的にスリープ状態になるため、低消費電力

- **リアルタイム再生＆オフラインレンダリング**
  - MIDI メッセージをリアルタイムに処理し、音声出力可能
  - MIDI 再生結果を WAV ファイルとして書き出し可能

---

### ymfmidi for Windows 常駐版

![ymfmidi for Windows 常駐版](img/ymfmidiwin_ss.png)

---

### ymfmidi for Windows コンソール版

![ymfmidi for Windows コンソール版](img/ymfmidiwin_ss2.png)
