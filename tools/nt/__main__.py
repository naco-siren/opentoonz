"""`python3 tools/nt ...` is the same as tools/nt/nexttoonz."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ntpy.cli import main  # noqa: E402

sys.exit(main())
