# Give this browser's file1 mid-game progress: HC, quest bits, 7 Days counters.
js='''(function(hc,q,raids,days){var p="/Save/file1.sav",j=JSON.parse(new TextDecoder().decode(FS.readFile(p)));var b=j.sections.base.data;b.healthCapacity=hc;b.health=hc;b.inventory.questItems=q;var c=j.sections.sevenDays.data.base.counters;c.hordeNightsSurvived=raids;c.daysSurvived=days;FS.writeFile(p,JSON.stringify(j));window.sohPersist();return [hc,q,raids,days].join(",")})(%d,%d,%d,%d)'''%(HC,Q,RAIDS,DAYS)
print(ev(js)); time.sleep(3); page.reload(); time.sleep(25)
exec(open('/tmp/z4155/rig/st.py').read())
