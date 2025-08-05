# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

JPEG Archive は、写真のアーカイブとWeb配信向けに最適化されたJPEG画像圧縮ツールセットです。知覚的視覚品質を維持しながら、JPEGファイルサイズを30-70%削減します。

## ビルドとインストール

### 前提条件
このプロジェクトはmozjpegライブラリに依存しています。

### macOSでのビルド
```bash
# mozjpegをインストール
brew install mozjpeg

# ビルド
make

# インストール（オプション）
sudo make install
```

### Linuxでのビルド
```bash
# mozjpegをビルドしてインストール
git clone https://github.com/mozilla/mozjpeg.git
cd mozjpeg
autoreconf -fiv
./configure --with-jpeg8
make
sudo make install

# jpeg-archiveをビルド
make

# インストール
sudo make install
```

### クリーンビルド
```bash
make clean
make
```

## テスト実行

### ユニットテスト
```bash
make test
```

### 統合テスト
```bash
cd test
./test.sh
```

## 主要コンポーネント

### 実行ファイル
- **jpeg-recompress**: メインの圧縮ツール。知覚的品質メトリクス（SSIM、MS-SSIM、SmallFry、MPE）を使用してJPEGを再圧縮
- **jpeg-compare**: 2つのJPEG画像の類似度を比較
- **jpeg-hash**: 画像のハッシュを生成して高速比較を可能にする
- **jpeg-archive**: 複数ファイルの並列処理用シェルスクリプト（ladonまたはGNU Parallelが必要）

### ソースコード構造
- `src/`: 共通ユーティリティとアルゴリズム実装
  - `util.[ch]`: JPEG処理の共通関数
  - `edit.[ch]`: 画像編集機能（フィッシュアイ補正など）
  - `hash.[ch]`: 画像ハッシュ生成
  - `smallfry.[ch]`: SmallFryアルゴリズム実装
  - `iqa/`: 画像品質評価（IQA）ライブラリ（SSIM、MS-SSIM、PSNR実装）

### 画像品質メトリクス

このプロジェクトは4つの画像比較メトリクスをサポート：

1. **SSIM** (デフォルト): 構造的類似性指標
2. **MS-SSIM**: マルチスケール構造的類似性（より正確だが遅い）
3. **SmallFry**: 線形重み付けBBCQ風アルゴリズム
4. **MPE**: 平均ピクセル誤差

## 開発時の注意点

### mozjpegの場所
- macOS: `/usr/local/opt/mozjpeg`
- Linux: `/opt/mozjpeg`
- FreeBSD: `/usr/local/lib/mozjpeg`
- Windows: `../mozjpeg`

環境変数`MOZJPEG_PREFIX`で変更可能です。

### Windows CI
AppVeyorを使用してWindows用バイナリを自動ビルド。MinGW環境でコンパイルされます。

### 品質設定
jpeg-recompressの品質プリセット：
- `low`: 低品質（高圧縮）
- `medium`: 中品質
- `high`: 高品質
- `veryhigh`: 最高品質（低圧縮）

### 並列処理
jpeg-archiveスクリプトは`ladon`または`parallel`を使用してマルチコア処理を実現。大量の画像処理時に推奨。