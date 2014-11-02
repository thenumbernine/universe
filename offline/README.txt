DIRECTORY LAYOUT:

universe/
	datasets/
		allsky/
			source/	<- put the 2MASS allsky survey gz files here.  gzip files are found at ftp://ftp.ipac.caltech.edu/pub/2mass/allsky/  
		2mrs/
			source/	<- put the 2MASS redshift survey here.  archive is found at http://tdc-www.cfa.harvard.edu/2mrs/
				2mrs_v240/	<- should be the only contents of the source dir
		sdss3/
			source/ <- put the SDSS3 DR9 2gig FITS file here.  file found at http://data.sdss3.org/sas/dr10/sdss/spectro/redux/specObj-dr10.fits
																more instructions found at http://www.sdss3.org/dr10/data_access/bulk.php
				specObj-dr10.fits	<- should be the only contents of the source dir

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
	converts datasets/6dfsgs/source/* to datasets/6dfgs/points/*.f32
1D) convert-sdss3
	converts datasets/sdss3/source/specObj-dr10.fits to datasets/sdss3/points/*.f32
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
	

