DIRECTORY LAYOUT:

universe/
	datasets/
		allsky/
			source/	<- put the 2MASS allsky survey gz files here.  gzip files are found at https://irsa.ipac.caltech.edu/2MASS/download/allsky/	
		2mrs/
			source/	<- put the 2MASS redshift survey here.  archive is found at http://tdc-www.cfa.harvard.edu/2mrs/
				2mrs_v240/	<- should be the only contents of the source dir
		6dfgs/
			source/ <- put the 6DFGS 6dFGSzDR3.txt file here, file found at http://www-wfau.roe.ac.uk/6dFGS/download.html
		sdss/
			source/ <- put the SDSS DR16 ~7-gigabyte FITS file here.  file found at http://data.sdss3.org/sas/dr16/sdss/spectro/redux/specObj-dr16.fits
				specObj-dr16.fits	<- should be the only contents of the source dir
		simbad/
			results.lua	<- generated from the convert-simbad.lua script
		gaia/
			source/	<- put the FITS file here, should be found at http://gea.esac.esa.int/archive/ adql form with the query...
						"select source_id, ra, dec, parallax, pmra, pmdec, radial_velocity, teff_val, radius_val, lum_val from gaiadr2.gaia_source where pmra is not null and pmdec is not null and radial_velocity is not null order by source_id asc"
					... 7,183,262 have pmra, pmdec, radial_velocity (from "select count(source_id) from gaiadr2.gaia_source where pmra is not null and pmdec is not null and radial_velocity is not null")
					... 6,081,418 have pmra, pmdec, radial_velocity, lum_val (from "select count(source_id) from gaiadr2.gaia_source where pmra is not null and pmdec is not null and radial_velocity is not null and lum_val is not null")
					... 6,081,418 have pmra, pmdec, radial_velocity, teff_val, radius_val, lum_val 


				how many have ra, dec, lum_val, teff_val, radius_val?  just possibly missing velocity.
					... 76,956,778 have teff_val, lum_val, radius_val from "select count(source_id) from gaiadr2.gaia_source where lum_val is not null and teff_val is not null and radius_val is not null;"

				The Gaia online query only has a maximum of 3000000 rows so you will have to add 'offset 3000000' and 'offset 6000000' etc to get all possible files.
				Name them: 1.fits 2.fits 3.fits
		gaia-dr2-dist/
			source/
				fits file goes here

					select gaia.source_id, gaia.ra, gaia.dec, gaia.teff_val, gaia.radius_val, gaia.lum_val, dist.r_est 
					from gaiadr2.gaia_source as gaia 
					inner join external.gaiadr2_geometric_distance as dist 
					on gaia.source_id = dist.source_id 
					where gaia.teff_val is not null 
					and gaia.lum_val is not null 
					and dist.r_est is not null 
					;

					maybe also query radius_val too if you want, but what's that good for, except when you are closely orbitting the star 

					and even though there are 1.3 billion dist calcs, there are only 76,956,778 that include temp and lum
					so ... how to download that in 3mil row chunks ... 

			Tha Gaia DR2 + Max Planck extra distance calculations on 1.3 billion stars

			- ok new Gaia DR3 -

				select count(source_id) from gaiadr3.gaia_source as gaia:  1811709771

				select count(gaia.source_id) from gaiadr3.gaia_source as gaia; should be same

				-- select gaia.source_id, gaia.ra, gaia.dec, gaia.teff_gspphot, params.lum_flame, params.radius_flame, params.mass_flame
				select count(gaia.source_id)
				from gaiadr3.gaia_source as gaia
				inner join gaiadr3.astrophysical_parameters as params
				on gaia.source_id = params.source_id
				where gaia.ra is not null
				and gaia.dec is not null
				and gaia.teff_gspphot is not null
				and params.lum_flame is not null
				and params.radius_flame is not null
				and params.mass_flame is not null
				;


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

1D) convert-sdss
	converts datasets/sdss/source/specObj-dr16.fits to datasets/sdss/points/*.f32
	then copy datasets/sdss/points/points.f32 to ../sdss-dr16.f32

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
	converts datasets/gaia/source/result.fits to converts datasets/gaia/points/*.f32


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

