

// This is a font character that loads a texture and recolors it.
class FFontChar1 : public FTexture
{
public:
   FFontChar1 (FTexture *sourcelump);
   const uint8_t *GetColumn(FRenderStyle style, unsigned int column, const Span **spans_out);
   const uint8_t *GetPixels (FRenderStyle style);
   void SetSourceRemap(const uint8_t *sourceremap);
   void Unload ();
   ~FFontChar1 ();

protected:
   void MakeTexture ();

   FTexture *BaseTexture;
   uint8_t *Pixels;
   const uint8_t *SourceRemap;
};

// This is a font character that reads RLE compressed data.
class FFontChar2 : public FTexture
{
public:
	FFontChar2 (int sourcelump, int sourcepos, int width, int height, int leftofs=0, int topofs=0);
	~FFontChar2 ();

	const uint8_t *GetColumn(FRenderStyle style, unsigned int column, const Span **spans_out);
	const uint8_t *GetPixels (FRenderStyle style);
	void SetSourceRemap(const uint8_t *sourceremap);
	void Unload ();

protected:
	int SourceLump;
	int SourcePos;
	uint8_t *Pixels;
	Span **Spans;
	const uint8_t *SourceRemap;

	void MakeTexture ();
};

