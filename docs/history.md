# History rewrite, 2026-09-23

On 2026-09-23 every commit message on `main` was rewritten to remove the
attribution trailer `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
102 of the 103 commits carried it. The Stage-0 import commit did not, so its
message was left byte-for-byte alone and its hash is unchanged.

**Nothing else changed.** File contents, commit order, the parent graph,
subject lines, message bodies, author dates and committer dates are all as
they were. The rewrite touched trailer lines and nothing more.

The tool was `git-filter-repo` 2.47.0 with `--prune-empty never` and
`--prune-degenerate never`, so no commit was dropped, collapsed or reordered.

## Why the contents are provably unchanged

A commit's tree hash is a content address: it is a hash of the whole file
tree at that commit. Two commits with the same tree hash have identical
contents, and this can be checked rather than taken on trust.

Every commit's tree hash was recorded before the rewrite. After the rewrite
each new commit's tree hash was compared against its original's, joined
through the tool's commit map:

```
commits on main, before and after      103 / 103
tree hash identical                    103
tree hash differing                      0
author/committer date differing          0
subject line differing                   0
commit hash unchanged                     1   (the Stage-0 import)
```

Message bodies were checked separately: for all 103 commits, the original
message with the trailer lines removed equals the rewritten message exactly,
so no other text moved.

To repeat the check on any commit, take an old hash from the map below and
its new one, and compare `git rev-parse <new>^{tree}` against the recorded
tree hash. A pre-rewrite bundle of all refs was taken before the operation.

## Tags

All four tags are annotated and were re-pointed to the rewritten commits.
Tagger lines, including dates, are unchanged.

```
v0.1.0   040d9ff6aa -> 4bd05d92e7
v0.2.0   a656d9a9be -> 7ceb20440e
v0.3.0   f9fb0d181f -> 8c0fd6cb16
v0.4.0   efca9cb6e5 -> 616ca4194e
```

These are the commits the tags resolve to, not the tag objects themselves.
The tag objects were rewritten too, necessarily, since they name a commit.

## What this invalidates

Anything outside this repository that names a pre-rewrite hash still names
the pre-rewrite object, which is no longer reachable from `main`:

- **CI run identifiers recorded before 2026-09-23 refer to pre-rewrite
  SHAs.** A run cited in `docs/` or in a design record for a date before this
  one ran against the old commit. The run is still the run that happened, and
  its result still stands, because the tree it built is the same tree; only
  the commit hash it reports has changed. Use the map below to relate them.
- Pre-rewrite hashes cited inside this repository were updated in the same
  commit that added this file. The four that existed were in
  `docs/preregistration.md`: `48fd13d` to `126778b`, `2989984` to `57c70cc`,
  and `8ac3964` to `6f75527` at two places.
- `research/heldout.lock` was UNSET at the time of the rewrite and so carried
  no hash to update. This is the reason the rewrite was done now rather than
  later: the gated computation had not yet run, so no registration hash, CI
  run or digest had been committed to the lock. Doing it afterwards would
  have invalidated the one artefact whose whole purpose is to be fixed in
  advance.

## Ordering

The rewrite completed, and the rewritten head was pushed, **before** the
gated computation was run. The threshold-before-MDE ordering fixed by
`docs/preregistration.md` section 10 is therefore witnessed by commits that
exist on the rewritten history, not only on the old one.

## Recurrence

The trailer is disabled at the source in the generating client's settings,
and a `commit-msg` hook strips it as a second line of defence. Hooks are not
carried by a clone, so the hook is reproduced here rather than tracked:

```sh
cat > .git/hooks/commit-msg <<'EOF'
#!/bin/sh
perl -ni -e 'print unless /^\s*Co-Authored-By:\s*Claude\b/i
                      || /Generated with \[Claude Code\]/i' "$1"
EOF
chmod +x .git/hooks/commit-msg
```

The hook rewrites the message file in place and exits zero, so a commit is
never rejected for carrying a trailer; the trailer is simply removed. History
is not rewritten again.

## Old-to-new commit map

All 103 commits, oldest first.

```
a445de79fb26c9ba21cf19666589d3a61dfb0fc4  a445de79fb26c9ba21cf19666589d3a61dfb0fc4
bf0c1a5761fdfbaaa8974858313ce91b15c8f77b  991e52268afa1b02eef9e41d33b686fcc3f9560e
1980c43a68dde47a567e29f8b4d6cbcd961e3b69  b8ee1ef3936944fcf0cf65926e876bb7d4187fc4
6a5c063803cd61eb0a4a2d8c929d43c7141fbca3  28a3f51f999aec4b761b48019dff09dc34811549
f7c65e1fdc717b08cf4349516b51f51571a8fe35  9f1ffb6268a4b90e4093905b7e2eaa43cf2cb242
80baf08eaf1f5d54c7f71a13fb1b25db250b651f  cdd7f388e0ea68830132738730607757b842062b
65e61a109e5ccf88e2a7d47cb95ad5d9b7c7a35b  3350796a10978243909c8f39f228c13eb850fb88
964013cd1371ee9951c84ed867bb9bf9f4915010  06655fb0cdd65233793712a5ad9eabcfd1e7c0de
d616c5c980238b3e95e3c1890debae3bfa2894a2  3420faa066f6ef240fa01dcd74475da77d8a2ae5
58bf2f0c2a8dcadc27f92e33c17dc65f245397c2  f129a1880951fe2f408196e895882f70c08c1a3e
19a7af5b19c121397e2c4f190b706a1adf6e8d6e  1d59043609ad2ca735ecade63170a7ce9996863a
23a7d4b4a32c12572e6666ecb582b1d577cdce0e  426c686189b998c7138932b53fae0db70c2297f0
881089d667679738131896139c0d316fa100f540  7ad17f7d649e7a1fe71807a992af438d86a562f5
003cceab10d2d1841c180323b80deec576c95a0d  c895ba81b02a0db5859cf3d0ef02f21cb2fb5df7
fd060e312627619d6822af5aa4a3c00542de4ac9  b99d5166293e5b0962228d19512f533e2d988a6f
e320ae86dc5d337bd0eeea1633fc35b6c0fe4084  0744a2b61910e873556d6fa8482d4f84eee302ea
779f5486b9f8dc568ced0a1778b06563f17b6f79  a0996b4911dcfa3c694ad736db03518c964e1aa8
e9555ce2ec51a1334362476975e5ecbebd34987a  27f23c17802e35d22f2ad2ebf58c2cce93c719bf
cca758a9a4240d2d3e3630e341a576b5acfc2a4d  e786e6d74d67438b33cdbcd9f1614fbe89de03a4
39fb44ce6ce3c3d49c596018a1887bb8d9c8fe2e  3de9a0715c77ca49b2233f8287dc96dec1453fd2
ff7fb18970d667aa13dba3e28ee5aeae52617e8d  5d60577d0b59a7af9423ed3792bc776684d8ec1a
4fc481ff93fcbcec99d2607ab6b3c98903dcc55f  fb2679dda423ae5ac3bf26c60ab673101e9a72da
5a7bc3e3068c8a8a3f35d5414ca03c5bebf2e437  017b1de061d9b0fda445be004053a4622f7595b7
fbc03245eaac109be8227d858d9e7526cae9bc45  ed199a5d41aec8a578f5b6130db615c0ff009bda
a8118c3555bfc3e54ce28b645c0599a12d187e15  7d47e7b37a6f3f39def1e6f27c5fc939e8d259a0
f800efc753171db3b637997355dcaa4a02b53585  a771bfa913601fc2198b0b4b08ef95be253a8c8b
acb83ebb153082ed0afbdf753b0eab1f5caeb855  f9f219c89657743c27d9640a3331a16daf0bad20
8216c83fd24bffb5c63febcbe3767d59d4fa6f29  54beebff8de235d39a6e31d1e9ae04dc45869240
040d9ff6aa0b483296de06f1d873c9e42c31f0a1  4bd05d92e7f40b6737dacacfbe38715fab1f5dd5
5162fa879521963c72cd34c61879d4e15cc4f19a  db2d652d6e8249900f6cfae95288a9b5931eb786
482110b7e810ef5cd45fd49275f669c7ccd09526  5278aefeb7fb37f24130d81950801ed41ed59f34
bda9fdb6eb179a43352086a2b1313d936a69d3fa  8b5406bf251b06e9c06a01f688ad7cfef85d6bb6
d4be4be611d8e09d1a06115b75fedd2f3b068d68  1205aca92c33e989e6adaf43f1ea0b45dea5c106
a00b7c5247e55d974b92588d9f11bf17494d7c57  8e99e2527d3a288f0d5521fc6a71e808228c539c
63dc8204d69a99a3aaf781a8d1f0fd4678b86215  6086f07ebfdb8438edd118f7d49bf68073d5b403
825a9606f0d83613e7156e7d5fe9c87f5e0bbe16  8b19b03150bbcc80652774c8c19c7aff6ab5b2c5
356cf1722827af5894a3e1e6a5d33dd85022453e  7353c9f052528a70804a4eb9bc6a8b41e597ce89
9c1f2873e0e2d1c34fd79aa6d5c97bdc0fd8daa1  6d19c724f1855512857500c6d1bf7d87d74195ad
dce8aad3a751653a4236cca1aa9fb23d76ebb35f  c77e434918d82ee53f9e83f68642ce16f47d7832
cb25dcc22e5a5fe0c041ca8f403a0da3686e6bf5  00846d1d5574223f8503e9ac85dc29952eb40ec5
f19fabe4c73b628b57a5bfbf9db83cd83552e49a  b809b55b88464323a5cd479b2e56f4c94fe38bf0
f5b59e50f9fbef1426a0f7f498f4e58ffdca63df  103076eb459f838b22dc68756bda50e38898e337
a735f811ae656dfe776f8cd64d585bb697836845  15c958bb5dbc07778918dbffe1452f3af684474e
1190eb5a8856027f01f1252445683593b6325a12  020b7bb7d6d451759a86388298932e5945da87dd
b3a48380227999af686d417f480cb0b5445298d3  82c587a5d6e4e284c58781536d92561009e6bc31
4a4c85748b56a5dbeca14e074fa82c1e78ccd6d4  d71ff0e8abf7348bad0b5858755c4c273c0ca7e2
82062e278f2bc5b7cd286611b1cca0a54bdc38ef  152a860b42f1ff8cd9ceaa6c3b82ab2eebaa81ec
74c8928744594bb534028afb0ed9a083f239075e  8d133bd282179a0b17e64fb9e92f5e5b1d833ec0
7f79a2c058bdf59242069a50eab5cc6e053516d0  d4e83f2df700309ef911c1306043357f8d270614
ab06336fd04509b4133adab381b4d8fe55c98e9a  d29d077f6d53abdd15001e08192947c14536a441
8d02d5b254086ef63104c4ca422e5426b28f4604  891225c0b902d1096644bbc57cac661522842cfb
37bf17d35572e65c95e78b2a7781c9656f30e903  f61fe1cb33b19cce2ea7350c415f7d40fa07c759
ebff1642edbfde578c908791954c257caa6cc4a1  d380e2ee034c63662f01efdff58f3c4936346070
61e1d242adce08760a4b18c2d0d50fd825c2a9cc  f29e88580e3a8913ff2ea1ce95b5da42300df825
c28629d42d0d1248a54574b78bfb3c99998a2577  bb2a0bd5f288115f4a1f7b5914483fa4d0b4158e
09fddf1d15e7ea479d1658b660412a9791e15192  d60282b9a1cf07c3e1bcbea18d26389597192e48
15587124c1bc5fa2d5c8d65253c3911c407ba779  c72c1ceac61d94efdd856e50338c08d6c8073786
45026c715b2de34f9b65129932c627ff762de9da  feacb00fa12f79338cac276500bfcf57d015418f
0151c3a45de63e1ae95ca0944e7b650bf90a1ce9  3f5c99f62dcf7bca0e3ba3d9f2c82cc7f20b2899
d39df3bc5bfc7659ca57007f9ef9f0dea6f81854  836137f780bbb9a0fe1f2086b69ccadd7530029d
5b6e9832fcfbe5914e46a55a1d76baf996b41f79  bbd8832adbc0a8ed6910375837f54f4c2914b76d
059e90a903972e13d60704ebb747585b76427bda  6b552ed5f4b76510244af1983387a74400429895
49071f37a128b6fe1bb36b9f514cdb46a7601d33  220ca24faf041c740cc513c0603d025003a2cce8
e97f72164beec23338af40d6e2c4480fee356e03  eba09a41b7d68274e52c185e5b86caafc81c5a23
6b259567a2446cbec093c391bbb812c407b1efcb  9548965b03c7c8443a5dd208e61a835b55cb1be5
1820166089a503678f6d9d3eccf4af7a1edb7860  95a01298ca9af746a5e131e918577d775e06602f
7183c9e56f01e2c632cd3dffbe1216a5e746f80e  c18c0ad0759885b09eb1a3647485415902428608
2fc149ddccb5c4edfd2f6e0ecfe63579beb3e5b3  3743777fde412b42562a3ee7ee1688fb53f229a3
89224c225f9d855f009e27b6cd57d77f7285c721  1c98c3d7332bc28fa498a4fa62a118b6f86436b3
cd5c579e2d7b6c21e58be14ac4c9110403f8e3fa  75d096b106baac3e83d28700ab3233e43ea7f669
52f594761983912c8a60ec9a02d54a4399cedcf1  3bf34b18d97978c0a4aef5dad97a9ae7544f1ead
350d1ccc91e28576593a976700f459a0d98ffd99  161ea40edbd33e043d02ec6141bd9a612327f54e
b86f869aad719e3ad537db9c1cb17ff7edc778f1  c56e37ef589adbdaa207cb9cb97fed2d61ccb852
91b909b77f7cd855e38b5895ab98de6cc6fffa07  678f8600b72999814d07fb864605e658b3665aa6
ccc1a4299273bc9718070fdd8866ffe667a43718  f5f6703ab8e06fa35597eeb0648f6bf49e47d01f
bc67eb6fa3e61e3dabf9f45b21948177c3565971  f82f4204beb1829ed24d15c2da3aeea19039b0ce
ebe80408d21da2a970135483491ec491842af8ee  44a2d9670cf40d1ae71711d84e1a29b4b3a95a23
8629c0b87a662703c14d5b27c5e156e4017ed2f9  b209137d643bb47094c3baf1619a61ce0bd37370
1b4c37cf890340a9fc6581488983bfa074e91ba5  90e67394ee8a127a7f6824c9596bc34508c82ef8
dcf56d5255493df2d02499c74c5ec55daf17f979  728e7bb1166fd6016a3cf7e8c0fb4d3e29f68218
a656d9a9be6de62c03cb25831b746cb236328f8a  7ceb20440edcb151a72841955352cf189b621955
207ef5aae8c58571bccccaba654697cc3ca036f9  ea4b4037453851a96ff38625811f0dd1ced7fd00
f9fb0d181fcc88ad84a47d350368e043e669bf2d  8c0fd6cb16e6919c11cbaf0976bc0599a8db23db
1fff3566ab0f800ffa3fe6dfde04c8bf16c63fd9  f51a239b19db4e6398eed03db272f5cb73638f4c
768db2566c82a6378e872099f0fa97ab5702e7da  8ed689d6d349e96244a15284ac669d4c4d7fdf0d
07d9b1eddd03850e7c2ffbb2d20a540e02c83c88  c728c25d9c65833137d8078959b5673d9dcd6421
4761fdce4814aa892b8acf0d7242f8ea7a18e826  21e2b6c704d8bf4c74b51b42056cf406383428ed
3cfa3532cb599c3ee3653260e699b3b390d4cfc8  b7be40c5c703e473c737a6839f4bf77775446583
efca9cb6e59b3fca3ed9d8aaa7ac1761437d92b9  616ca4194e77093d2826c40adc5501e043c69418
487969f1d7f577f4dd401b1a29d3a92d5747692e  e309ac3be22ef925377fdba5974ff85d8ed4bf3a
f5f316722ad831bb418c8ef11199fdfb6c80e287  0fc4466a067cf4283ed6be555f72cfc1fd6f5f4c
a62769064eb185a8c2f1214ee75904062143ff51  7fd1b84cfe32b145f4e4187ea935284789d67e6b
298998456a59e17b3afa194dcd139ac216100841  57c70cc6507e8665d09b9473eade3eceefff7eb4
8ac3964b64f99e6a364cd56d65859fbddbe670da  6f755279150610ee9f18f652e16f516a9b3dda8f
48fd13d7c6d4b3daf87bea77925b3af9153a2169  126778ba3c9c8f0cba3d69cad26f32edc109c112
f2a948ad79eb66540334196cf476aa47c863c495  7c8301bd255292f275c1d350a064a95e7589ad8d
eb4999ee080967720126274fbc5bc030b5cd7943  9a1aad764ef3c2c1a82712d89df0a5fd480640d3
c40b8ebfffe0af0440de77957b03524f39ece219  0e65b2a64dc3d1b4e3f2d629b14b447bbb1557b8
d3d6e1b3b84f852e8a767ab6e2d24fe60ee352eb  e480306f49b84d4d2f27651ec7446a969baa7035
df6b0522af67d4979544e792acb22cd49be97bee  2c5c53569396cd68bc219faf704d45c3fa5a7f18
91a57350f7a6c2d1d648462284c89a0fa405927b  985d66f435fa4a86e896035fdf1ae732403346cf
e4a4a4c7f7665da05a9a79d3bb59adedc3aa0f39  7c01bf35cda3a18f3ba54a3a09c57d34d75d57ee
991d2febe607cdb7441154323f2a8006c967c975  0b397511d0ded3a3be4ff5f9d9ce7171d0c038a9
```
