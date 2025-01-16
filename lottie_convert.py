#!/usr/bin/env python3

import re
import sys
import os
import argparse
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "lib"
))
from lottie.exporters import exporters
from lottie.importers import importers
from lottie.utils.stripper import float_strip, heavy_strip
from lottie import __version__


desc = """Converts between multiple formats

Supported formats:

- Input:
%s

- Output:
%s
""" % (
    "\n".join("%s- %s" % (" "*2, e.name) for e in importers),
    "\n".join("%s- %s" % (" "*2, e.name) for e in exporters),
)

parser = argparse.ArgumentParser(
    description=desc,
    formatter_class=argparse.RawTextHelpFormatter,
    conflict_handler='resolve'
)
parser.add_argument("--version", "-v", action="version", version="%(prog)s - python-lottie " + __version__)

group = importers.set_options(parser)
group.add_argument(
    "infile",
    default="-",
    nargs="?",
    help="Input file"
)
group.add_argument(
    "--input-format", "-if",
    default=None,
    choices=[importer.slug for importer in importers],
    help="Explicit output format (if missing implied by the input filename)",
)


group = exporters.set_options(parser)

group.add_argument(
    "outfile",
    default="-",
    nargs="?",
    help="Output file"
)
group.add_argument(
    "--output-format", "-of",
    default=None,
    choices=[exporter.slug for exporter in exporters],
    help="Explicit output format (if missing implied by the output filename)",
)
group.add_argument(
    "--sanitize",
    default=False,
    action="store_true",
    help="Ensure the animation is 512x512 and 30 or 60 fps",
)
group.add_argument(
    "--optimize",
    "-O",
    default=1,
    type=int,
    choices=[0, 1, 2],
    help="Optimize the animation parameter:\n" +
         " * 0 no optimization\n" +
         " * 1 truncate floats\n" +
         " * 2 truncate floats and names"
)
group.add_argument(
    "--fps",
    default=None,
    type=int,
    help="If present, changes the output fps to match this value"
)
group.add_argument(
    "--width",
    default=None,
    type=int,
    help="Override output width",
)
group.add_argument(
    "--height",
    default=None,
    type=int,
    help="Override output height",
)


def print_dep_message(loader):
    if not loader.failed_modules:
        return

    sys.stderr.write("Make sure you have the correct dependencies installed\n")

    for failed, dep in loader.failed_modules.items():
        sys.stderr.write("For %s install %s\n" % (failed, dep))


if __name__ == "__main__":
    ns = parser.parse_args()
    if ns.infile == "-" and not ns.input_format:
        parser.print_help()

    infile = ns.infile
    importer = None
    if infile == "-":
        infile = sys.stdin
    else:
        suf = os.path.splitext(infile)[1][1:]
        for p in importers:
            if suf in p.extensions:
                importer = p
                break
    if ns.input_format:
        importer = None
        for p in importers:
            if p.slug == ns.input_format:
                importer = p
                break
    if not importer:
        sys.stderr.write("Unknown importer\n")
        print_dep_message(importers)
        sys.exit(1)

    outfile = ns.outfile
    exporter = None
    if outfile == "-":
        outfile = sys.stdout
    else:
        exporter = exporters.get_from_filename(outfile)
    if ns.output_format:
        exporter = exporters.get(ns.output_format)

    if not exporter:
        sys.stderr.write("Unknown exporter\n")
        print_dep_message(exporters)
        sys.exit(1)

    i_options = {}
    for opt in importer.extra_options:
        i_options[opt.name] = getattr(ns, opt.nsvar(importer.slug))

    o_options = exporter.argparse_options(ns)

    an = importer.process(infile, **i_options)

    if ns.fps:
        an.frame_rate = ns.fps

    if ns.width or ns.height:
        width = ns.width
        height = ns.height
        if not width:
            width = an.width * height / an.height
        if not height:
            height = an.height * width / an.width
        an.scale(width, height)

    if ns.optimize == 1:
        float_strip(an)
    elif ns.optimize >= 2:
        heavy_strip(an)

    exporter.process(an, outfile, **o_options)
