"""Record the command words ("games", "music") using record_clips.py."""
import record_clips as rc

WORDS = ["games", "music"]
CLIPS_PER_WORD = 20
STYLES = ["normal voice", "quickly", "slowly", "loudly", "quietly", "from arm's length"]

def command_plan(*args, **kwargs):
    plan = []
    for word in WORDS:
        for i in range(CLIPS_PER_WORD):
            plan.append((word, f'Say "{word}"  ({STYLES[i % len(STYLES)]})'))
    return plan

rc.build_plan = command_plan
rc.main()
