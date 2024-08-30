
struct UniformBufferObject
{
	mat4 ModelView;
	mat4 Projection;
	mat4 ModelViewInverse;
	mat4 ProjectionInverse;
	mat4 ViewProjection;
	mat4 PrevViewProjection;
	vec4 ViewportRect;
	
	float Aperture;
	float FocusDistance;
	float SkyRotation;
	float HeatmapScale;
	float ColorPhi;
	float DepthPhi;
	float NormalPhi;
	float PaperWhiteNit;
	vec4 SunDirection;
	vec4 SunColor;
	vec4 BackGroundColor;
	float SkyIntensity;
	uint SkyIdx;
	uint TotalFrames;
	uint MaxNumberOfBounces;
	uint NumberOfSamples;
	uint NumberOfBounces;
	uint RR_MIN_DEPTH;
	uint RandomSeed;
	uint LightCount;
	bool HasSky;
	bool ShowHeatmap;
	bool UseCheckerBoard;
	uint TemporalFrames;
	bool HasSun;
	bool HDR;
	bool AdaptiveSample;
	float AdaptiveVariance;
	uint AdaptiveSteps;
	bool TAA;
	
	uint SelectedId;
};
