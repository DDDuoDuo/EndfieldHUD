# Offline Earth terrain

The Map module uses real Earth relief from NOAA **ETOPO 2022**, not a generated height field. Geographic world coordinates use an equirectangular projection: the vector rectangle is 1024 × 512; longitude −180° is x=0, longitude +180° is x=1024, latitude +90° is y=0 and latitude −90° is y=512. Pins can therefore be persisted geographically independently of the viewport.

Source: [NOAA ETOPO Global Relief Model](https://www.ncei.noaa.gov/products/etopo-global-relief-model). Data are [CC0-1.0](https://www.ncei.noaa.gov/access/metadata/landing-page/bin/iso?id=gov.noaa.ngdc.mgg.dem%3Aetopo_2022). Citation: NOAA National Centers for Environmental Information. 2022: ETOPO 2022 15 Arc-Second Global Relief Model. DOI: **10.25921/fd45-gt74**. Accessed 2026-09-27.

## Build-time preparation

`scripts/generate-world-map.py` requests a 1440 × 720 stride-60 subset through NOAA's [ERDDAP dataset](https://coastwatch.pfeg.noaa.gov/erddap/griddap/ETOPO_2022_v1_15s.html):

```
https://coastwatch.pfeg.noaa.gov/erddap/griddap/ETOPO_2022_v1_15s.nc?z[0:60:43199][0:60:86399]
```

This is about 4 MiB of floating-point elevation samples at 15-arcminute spacing. The preparation step smooths source-grid stair steps by 0.65 sample pixels, extracts sea level and elevation contours, and simplifies vector paths to 0.10 world-map units. The longitude seam is closed and polar values extend to the boundary. Isolated features smaller than a fraction of a world-view pixel are omitted. Bathymetry has contours at −4000 and −2000 metres; land has coastlines and contours at 250, 500, 1000, 1500, 2000, 3000, 4000, 5000 and 6000 metres. Land bands retain closed outer boundaries and interior holes.

To reproduce, use Python with NumPy, SciPy and ContourPy, then run:

```sh
python3 scripts/generate-world-map.py --source /tmp/etopo2022-15min.nc
```

If the source file does not exist, the script downloads that bounded subset. It is a developer tool and is never called by the app. No preprocessing dependencies ship with the app. `SOURCES.json` contains source and output hashes, byte size, and exact path/vertex counts.

## Runtime

The bundle contains only compact vector coordinates, not the elevation raster. `WorldMapTerrain` decodes each elevation into retained `CGPath` objects once, on demand. Individual coordinates are unsigned 16-bit values spanning the world rectangle. Filling uses the even-odd rule to preserve inland holes. The loader caps the resource to 3 MiB and 150,000 vertices; malformed/truncated data is rejected. The raster worker clips and simplifies those immutable paths into a bounded viewport image. Panning and zooming immediately transform that image; coalesced background redraws refresh its coverage and detail. The map controller owns visibility and pin pulse animations so closed HUDs need no map work.

The generalized relief is intended for a stylized world overview. Zoom does not fetch street-level or survey-grade detail. It is not suitable for geographic navigation. The source and derivative are independent of the game reference, whose dark technical styling and pulsing target-marker motion inspired the UI.

## Country plates

The raised country plates use the independent [Natural Earth 1:10m Admin 0 Countries](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-0-countries/) vector source, version **5.1.1**. Natural Earth data are [public domain](https://www.naturalearthdata.com/about/terms-of-use/). Exact archive and output hashes are in `Resources/WorldMap/Countries-SOURCES.json`.

The app's requested presentation combines source records **CHN, TWN, HKG and MAC** into one country plate named **China**, with identifier **CHN**. The original Natural Earth classifications are not rewritten; this is a documented application-level grouping in the derived resource. Geometry is unioned before simplification so adjoining components do not acquire artificial internal plate outlines. All other country records retain their source identifiers and English names.

The offline generator retains separate polygon components and interior holes. Topology-preserving simplification uses 0.085 world-map units globally and 0.006 units for the grouped China geometry, retaining more coastline detail for the requested Shenzhen view. Coordinates are quantized to 24 bits per axis, stored in 32-bit fields; this avoids visible coordinate stepping at close zoom. Natural Earth's cartographic data do not become street-level detail when zoomed in.

Reproduce using Python with `pyshp` and `shapely`:

```sh
python3 scripts/generate-map-countries.py --source /tmp/ne_10m_admin_0_countries.zip
```

If absent, the developer script downloads the official 4.7 MiB Natural Earth archive. The runtime country decoder accepts only the bounded `Countries.bin`, with a 1 MiB byte cap and 100,000-vertex cap. `CGPath` objects are retained. Per-component bounds and worker-owned spatial indexes skip remote islands and other offscreen geometry. The worker rebuilds clipped render geometry only when the padded image needs refreshing; pointer events do not rebuild the full source paths.
