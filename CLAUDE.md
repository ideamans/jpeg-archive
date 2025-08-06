# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

JPEG Archive は、写真のアーカイブと Web 配信向けに最適化された JPEG 画像圧縮ツールセットです。知覚的視覚品質を維持しながら、JPEG ファイルサイズを 30-70%削減します。

## ビルドとインストール

### 前提条件

このプロジェクトは mozjpeg ライブラリに依存しています。

### macOS でのビルド

```bash
# mozjpegをインストール
brew install mozjpeg

# ビルド
make

# インストール（オプション）
sudo make install
```

### Linux でのビルド

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

- **jpeg-recompress**: メインの圧縮ツール。知覚的品質メトリクス（SSIM、MS-SSIM、SmallFry、MPE）を使用して JPEG を再圧縮
- **jpeg-compare**: 2 つの JPEG 画像の類似度を比較
- **jpeg-hash**: 画像のハッシュを生成して高速比較を可能にする
- **jpeg-archive**: 複数ファイルの並列処理用シェルスクリプト（ladon または GNU Parallel が必要）

### ソースコード構造

- `src/`: 共通ユーティリティとアルゴリズム実装
  - `util.[ch]`: JPEG 処理の共通関数
  - `edit.[ch]`: 画像編集機能（フィッシュアイ補正など）
  - `hash.[ch]`: 画像ハッシュ生成
  - `smallfry.[ch]`: SmallFry アルゴリズム実装
  - `iqa/`: 画像品質評価（IQA）ライブラリ（SSIM、MS-SSIM、PSNR 実装）

### 画像品質メトリクス

このプロジェクトは 4 つの画像比較メトリクスをサポート：

1. **SSIM** (デフォルト): 構造的類似性指標
2. **MS-SSIM**: マルチスケール構造的類似性（より正確だが遅い）
3. **SmallFry**: 線形重み付け BBCQ 風アルゴリズム
4. **MPE**: 平均ピクセル誤差

## 開発時の注意点

### mozjpeg の場所

- macOS: `/usr/local/opt/mozjpeg`
- Linux: `/opt/mozjpeg`
- FreeBSD: `/usr/local/lib/mozjpeg`
- Windows: `../mozjpeg`

環境変数`MOZJPEG_PREFIX`で変更可能です。

### Windows CI

AppVeyor を使用して Windows 用バイナリを自動ビルド。MinGW 環境でコンパイルされます。

### 品質設定

jpeg-recompress の品質プリセット：

- `low`: 低品質（高圧縮）
- `medium`: 中品質
- `high`: 高品質
- `veryhigh`: 最高品質（低圧縮）

### 並列処理

jpeg-archive スクリプトは`ladon`または`parallel`を使用してマルチコア処理を実現。大量の画像処理時に推奨。

# libjepgarchive

一部の機能を静的ライブラリとしてエクスポートする。

- jpegarchive.h
- libjpegarchive.a

## jpeg_recompress

@jpeg-recompress.c のインターフェースを変更して機能を提供する。

### 入力 JpegRecompressInput

- jpeg byte[]
- length int64
- min int
- max int
- loops int
- quality enum (low|medium|high|veryhigh)
- method enum (ssim)

その他のオプションは jpeg-recompress のデフォルトを採用。

### 出力 JpegRecompressOutput

- errorCode enum (Ok|NotJpeg|Unsupported|NotSuitable|MemoryError|Unknown)
  - Ok 成功
  - InvalidInput 入力が無効
  - NotJpeg 入力が JPEG ではない
  - Unsupported 入力 JPEG がサポートされていない
  - NotSuitable 入力 JPEG が再圧縮に適さない
  - MemoryError メモリエラー
  - Unknown 不明なエラー
- jpeg byte[]
- length int64
- quality int
- metric double

## free_jpeg_recompress_output

メモリリークを防ぐため、JpegRecompressOutput のメモリを解放する関数。

- 入力
  - output JpegRecompressOutput
- 出力
  - なし

## jpeg_compare

jpeg_compare.c を元に JPEG の SSIM 比較を行う。入力値がメモリになることと method が実質 SSIM だけのことを除き、その他のパラメータはデフォルト値を採用する。

### 入力 JpegCompareInput

- jpeg1 byte[]
- jpeg2 byte[]
- length1 int64
- length2 int64
- method enum (ssim)

### 出力

- errorCode enum (Ok|NotJpeg|MemoryError|Unknown)
  - Ok 成功
  - InvalidInput 入力が無効
  - NotJpeg 入力が JPEG ではない
  - Unsupported 入力 JPEG がサポートされていない
  - NotSuitable 入力 JPEG が再圧縮に適さない
  - MemoryError メモリエラー
  - Unknown 不明なエラー
- metric double

## free_jpeg_compare_output

メモリリークを防ぐため、JpegCompareOutput のメモリを解放する関数。

- 入力
  - output JpegCompareOutput
- 出力
  - なし

## テスト

- @test/test-files 内のファイルを jpeg_recompress して、CLI 版の jpeg-recompress と SSIM、quality、ファイルサイズを比較する。
- それぞれ 5%未満の誤差であれば合格とする。
- 続けてその結果を元の画像と jpeg_compare して SSIM 値が CLI 版の jpeg-compare と同一になることを確認する。
- CLI は STDERR または STDOUT に結果を出力するので、それをパースして出力値とする。
