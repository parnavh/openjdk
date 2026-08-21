# MSc Dissertation

Hey!
I forked OpenJDK and modified some of its files and added some new files.

The pull request can be looked at [pull request changes](https://github.com/parnavh/openjdk/pull/1/changes) which will show all the modifications made.

You can build the jdk using instructions below or run:
```bash
nix develop # requires nix installed - will handle all dependencies
./scripts/build.sh

./scripts/ben-ren.sh 300 ./path/to/renaissance.jar # to replicate the experiment
```

You can find the renaissance jar from their [website](https://renaissance.dev/)

# Welcome to the JDK!

For build instructions please see the
[online documentation](https://git.openjdk.org/jdk/blob/master/doc/building.md),
or either of these files:

- [doc/building.html](doc/building.html) (html version)
- [doc/building.md](doc/building.md) (markdown version)

See <https://openjdk.org/> for more information about the OpenJDK
Community and the JDK and see <https://bugs.openjdk.org> for JDK issue
tracking.
