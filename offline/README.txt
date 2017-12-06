DIRECTORY LAYOUT:

universe/
	datasets/
		allsky/
			source/	<- put the 2MASS allsky survey gz files here.  gzip files are found at ftp://ftp.ipac.caltech.edu/pub/2mass/allsky/  
		2mrs/
			source/	<- put the 2MASS redshift survey here.  archive is found at http://tdc-www.cfa.harvard.edu/2mrs/
				2mrs_v240/	<- should be the only contents of the source dir
		sdss3/
			source/ <- put the SDSS3 DR14 5-gigabyte FITS file here.  file found at http://data.sdss3.org/sas/dr14/sdss/spectro/redux/specObj-dr14.fits
				specObj-dr14.fits	<- should be the only contents of the source dir
		gaia/
			source/	<- put the FITS file here, should be found at http://gea.esac.esa.int/archive/ adql form with the query...
						"select source_id, ra, dec, parallax from gaiadr1.gaia_source where parallax is not null order by source_id asc"
				results.fits

USAGE:

1A) convert-2mass --force --all
	converts datasets/allsky/source/*.gz to datasets/allsky/points/*.f32 vector of xyz
	values with no, nan, or inf of ra, dec, j_m, h_m, k_m are thrown away
	intermediate files are placed in datasets/allsky/raw/
1B) 
	I.	convert-2mrs
		converts datasets/2mrs/source/2mrs_v240/catalog/2mrs_1175_done.dat
			places results in datasets/2mrs/points/*.f32
			creates datasets/2mrs/catalog.specs that contain column sizes in bytes of datasets/2mrs/catalog.dat (yet to be created)
	II.	convert-2mrs --catalog
			uses datasets/2mrs/catalog.specs with datasets/2mrs/source/2mrs_v240/catalog/2mrs_1175_done.dat
				to create datasets/2mrs/catalog.dat
1C) convert-6dfgs 
	converts datasets/6dfgs/source/* to datasets/6dfgs/points/*.f32
	then copy datasets/6dfgs/points/points.f32 to ../6dfgs.f32 

1D) convert-sdss3
	converts datasets/sdss3/source/specObj-dr14.fits to datasets/sdss3/points/*.f32
	then copy datasets/sdss3/points/points.f32 to ../sdss3-dr14.f32

1E) convert-simbad.lua
	converts to datasets/simbad/points/points.f32
	it only uses entries with distance information, of which there are 41876 in simbad
	from there it only generates points for those beyond the milky way, of which there's only 6008
	and get-simbad-otypedescs.lua to write ../otypedescs.js and populate the otypeDescs array 
	then copy:
		datasets/simbad/points/points.f32 to ../simbad.f32
		datasets/simbad/catalog.dat to ../simbad-catalog.dat
		datasets/simbad/catalog.specs to ../simbad-catalog.specs

1F) convert-gaia
	converts datasets/gaia/source/results.fits to converts datasets/gaia/points/*.f32


2) getstats --force --all
	reads datasets/<set>/points/*.f32 data 
	writes datasets/<set>/stats/*.stats containing the number of points and the min/max/avg/stddev x/y/z
3) gettotalstats
	reads datasets/<set>/stats/*.stats files
	writes datasets/<set>/stats/total.stats
4) getstats --force --all --remove-outliers
	reads datasets/<set>/points/*.f32 data and total.stats
	writes datasets/<set>/stats/*.stats containing stats only pertaining to points within 3-stddev of the means of x, y, z
5)	gettotalstats
	reads datasets/<set>/stats/*.stats files permuted by the previous command
	rewrites datasets/<set>/stats/total.stats pertaining to only points within 3-stddev of mean of x,y,z
6A) show --all
	shows the data in a SDL OpenGL viewer
6B)	genvolume
	reads datasets/<set>/stats/total.stats and datasets/<set>/points/*.f32
	writes datasets/<set>/density.vol, containing float data ranged from 0-1 where 0 corresponds to the lowest density (nothing) and 1 corresponds to the highest density
	for use with web viewer
6C) genoctree --all
	reads datasets/<set>/stats/total.stats and datasets/<set>/points/*.f32
	writes datasets/<set>/octree/node*.f32, containing all points within the leaf node specified by the filename 

