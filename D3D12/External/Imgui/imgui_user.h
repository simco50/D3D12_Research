namespace ImGui
{
	inline void ImageAutoSize(void* textureId, const ImVec2& imageDimensions)
	{
		ImVec2 windowSize = GetContentRegionAvail();
		float width = windowSize.x;
		float height = windowSize.x * imageDimensions.y / imageDimensions.x;
		if (imageDimensions.x / windowSize.x < imageDimensions.y / windowSize.y)
		{
			width = imageDimensions.x / imageDimensions.y * windowSize.y;
			height = windowSize.y;
		}
		Image(textureId, ImVec2(width, height));
	}
}
