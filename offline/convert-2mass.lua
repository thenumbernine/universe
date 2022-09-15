--[[
require 'ext'
require 'htmlparser'
require 'htmlparse.xpath'
d = file'datasets/allsky/format_psc.html':read()
t = htmlparser.new(d):parse()
return htmlparser.xpath(t, '//tr'):filter(function(n) return #n.child > 3 and n.child[1].tag == 'td' end):map(function(n) return flattenText(n.child[1]) end):concat(' ')
--]]
require 'ext'
local ffi = require 'ffi'
require 'ffi.C.stdio'
local cols = [[ra dec/decl err_maj err_min err_ang designation j_m j_cmsig j_msigcom j_snr h_m h_cmsig h_msigcom h_snr k_m k_cmsig k_msigcom k_snr ph_qual rd_flg bl_flg cc_flg ndet prox pxpa pxcntr gal_contam mp_flg pts_key/cntr hemis date scan glon glat x_scan jdate j_psfchi h_psfchi k_psfchi j_m_stdap j_msig_stdap h_m_stdap h_msig_stdap k_m_stdap k_msig_stdap dist_edge_ns dist_edge_ew dist_edge_flg dup_src use_src a dist_opt phi_opt b_m_opt vr_m_opt nopt_mchs ext_key scan_key coadd_key coadd]]
cols = cols:split('%s+'):map(function(v,k) return k,v end)

function dist(magn, zeroMagnFlux, wavelength, restFlux)
	local flux = zeroMagnFlux * 10^(-.4 * magn) * 3e-6 / (wavelength * wavelength)
	local d = math.sqrt(restFlux / flux)
	d = d * 0.038618670083148
	return d
end

local begin = os.clock()
local fn = 'datasets/allsky/raw/psc_aaa'
local dstfile = ffi.C.fopen('datasets/allsky/points/test.f32', 'wb')
local vtx = ffi.new('float[3]')
local sizeofvtx = ffi.sizeof('float[3]')
for l in io.lines(fn) do
	local w = l:split('|')
	local ra = tonumber(w[cols.ra])
	local dec = tonumber(w[cols['dec/decl']])
	local j_m = tonumber(w[cols.j_m])
	local h_m = tonumber(w[cols.h_m])
	local k_m = tonumber(w[cols.k_m])
	local r = 0
	if j_m then r = r + dist(j_m, 1594, 1.235, 3.129e-13) end
	if h_m then r = r + dist(h_m, 1024, 1.662, 1.133e-13) end
	if k_m then r = r + dist(k_m, 666.7, 2.159, 4.283e-14) end
	if ra and dec and r > 0 then
		local rad_ra = math.rad(ra)
		local rad_dec = math.rad(dec)
		local cos_dec = math.cos(rad_dec)
		vtx[0] = r * math.cos(rad_ra) * cos_dec
		vtx[1] = r * math.sin(rad_ra) * cos_dec
		vtx[2] = r * math.sin(rad_dec)
		ffi.C.fwrite(vtx, sizeofvtx, 1, dstfile)
	end
end
ffi.C.fclose(dstfile)
local finish = os.clock()
print('took '..(finish-begin)..' seconds for file '..fn)
