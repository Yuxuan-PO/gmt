#!/bin/sh
#	$Id: sph_ex_4.sh,v 1.6 2011-07-19 21:59:41 guru Exp $
# Example of computing Delauney triangles with sphtriangulate
ps=`basename $0 '.sh'`.ps
# Use the locations of a global hotspot file
sphtriangulate hotspots.d -Qd -T > $$.arcs
# Make a basic contour plot and overlay voronoi polygons and coastlines
pscoast -Rg -JG-120/-30/7i -P -B30g30:"Delaunay triangles from hotspots": -Glightgray -K -X0.75i -Y2i > $ps
psxy -R -J -O -K $$.arcs -W1p >> $ps
psxy -R -J -O -K -W0.25p -Sc0.1i -Gred hotspots.d >> $ps
psxy -R -J -O -T >> $ps
gv $ps &
rm -f *$$*
