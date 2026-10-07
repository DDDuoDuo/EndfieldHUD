![EndfieldHUD 제목](docs/media/readme/endfield-hud-title.png)

<p align="center">
  <a href="https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0"><img src="docs/media/readme/version-badge.svg" alt="v1.2.0 릴리스" height="38"></a>
  <a href="https://github.com/DDDuoDuo/EndfieldHUD/issues"><img src="docs/media/readme/bug-badge.svg" alt="버그 제보" height="38"></a>
  <a href="https://space.bilibili.com/223936961"><img src="docs/media/readme/bilibili-badge.svg" alt="Bilibili의 DDDuoDuo" height="38"></a>
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a> · <a href="README.zh-TW.md">繁體中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.ko.md">한국어</a>
</p>

Mac에 *명일방주: 엔드필드*의 분위기를 조금 더해보세요. 단축키나 메뉴 막대에서 여러 레이어가 움직이는 HUD를 열고 메모, 파일, 음악, 게임 정보와 일상 도구를 이용할 수 있어요.

<a href="https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0"><img src="docs/media/readme/release-v1.2.0.png" alt="v1.2.0 릴리스" width="326"></a>

[PKG 다운로드](https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.0/EndfieldHUD-1.2.0-build18-Installer.pkg) · [DMG 다운로드](https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.0/EndfieldHUD-1.2.0-build18-macOS.dmg) · [모든 릴리스](https://github.com/DDDuoDuo/EndfieldHUD/releases)

<img src="docs/media/readme/en-01-installation.png" alt="설치" width="326">

0. EndfieldHUD가 이미 설치되어 있다면 메뉴 막대에서 **업데이트 확인** 버튼을 누르세요.
1. DMG를 열어 **EndfieldHUD.app**을 **응용 프로그램**으로 드래그하거나 PKG 설치 프로그램을 실행하세요.
2. 응용 프로그램에서 EndfieldHUD를 열고 메뉴 막대의 아이콘을 확인하세요.
3. [Ctrl + 백틱 (`)](docs/system-overlay.md)을 누르면 HUD가 열려요. **단축키**에서 키 조합을 바꿀 수 있어요.

macOS가 앱 실행을 차단하면 **시스템 설정 → 개인정보 보호 및 보안 → 확인 없이 열기**에서 실행을 허용하세요. 아직 Apple 공증을 받지 않은 앱이에요.

옆쪽 버튼으로 섹션을 바꾸고, 오른쪽을 스크롤하면 더 많은 섹션을 볼 수 있어요. **Esc**를 누르면 먼저 편집에서 빠져나온 뒤 HUD가 닫혀요. HUD를 닫아도 앱은 계속 실행되며, 빨간 전원 버튼을 누르면 앱을 종료할 수 있어요.

![HUD 열기와 닫기](docs/media/readme/opening.gif)

<img src="docs/media/readme/en-02-requirements.png" alt="시스템 요구 사항" width="326">

Apple silicon: **macOS 11 이상**. Intel: **macOS 10.15.4 이상**. 일부 도구에는 더 최신 시스템이나 지원되는 하드웨어가 필요해요.

권한이 필요한 기능을 사용할 때 해당 권한을 허용하세요.

| 권한 | 용도 |
| --- | --- |
| 손쉬운 사용 | 작업 모드에서 제어 센터를 통해 macOS 집중 모드를 전환해요. 권한이 없어도 타이머는 작동해요. |
| 시스템 오디오 녹음 | macOS 14.2 이상에서 실험적인 앱별 음량 조절에 사용해요. 오디오는 저장하지 않아요. |
| 자동화 | macOS가 요청할 때 허용하면 지원되는 음악 앱을 제어할 수 있어요. |
| 알림 | 업데이트 알림과 달력 미리 알림에 사용해요. |
| 파일 및 폴더 | 직접 선택한 파일, 사진, 동영상을 열 때 사용해요. |

권한은 **시스템 설정 → 개인정보 보호 및 보안**에서 확인할 수 있어요. 알림은 별도 설정 페이지에서 관리해요.

<img src="docs/media/readme/en-03-functions.png" alt="기능" width="326">

![HUD 섹션 전환](docs/media/readme/modules.gif)

| 도구 | 할 수 있는 일 |
| --- | --- |
| [메모](docs/notes-canvas.md) | 글을 쓰고, 그림을 그리고, 체크리스트나 이미지, 동영상을 추가해요. 패널을 고정하면 다른 섹션에서도 볼 수 있어요. |
| [임시 파일 보관함](docs/file-shelf.md) | 파일을 드롭해 미리 보고 다시 밖으로 드래그해요. 원본은 제자리에 남아요. |
| [클립보드 캐시](docs/clipboard-cache.md) | 최근 복사한 텍스트, 링크, 이미지, 파일을 스크롤하며 살펴봐요. 앱을 종료하면 기록이 지워져요. |
| [아카이브](docs/archive.md) | 서식 있는 텍스트와 미디어를 담은 문서를 원하는 분류로 보관해요. |
| [리더](docs/reader.md) | 소설과 PDF를 읽고, 책갈피를 추가하고, 확대해서 볼 수 있어요. |
| [미디어 편집](docs/media-assembly.md) | 사진과 동영상을 자르고, 보정하고, 필터와 스티커를 넣어요. |
| [화면 필기](docs/projection.md) | 화면 크기의 캔버스에 그림을 그려요. 점무늬 배경도 켤 수 있어요. |
| [지금 재생 중](docs/now-playing.md) | 앨범 아트와 제공되는 가사를 보고 재생을 제어해요. |
| [작업 모드와 음량](docs/audio-and-work-mode.md) | 집중 모드를 켜고, 오디오 장치를 바꾸고, [지원되는 음량 조절 기능을 사용해요.](docs/per-app-audio.md) |
| [달력](docs/calendar.md) | 타이머나 스톱워치를 실행하고, 미리 알림이 있는 일정을 저장해요. |
| [지도](docs/map.md) | 오프라인 지형 지도를 둘러보고 핀을 추가하거나 꾸며요. |
| [클로저의 미니게임](docs/orbipom-runtime.md) | '합체! 오르비폼!'을 즐겨보세요. |
| [이벤트 로그](docs/event-log.md) | 진짜 관리자처럼 이벤트 로그를 확인해보세요. |
| [개인 프로필](docs/personal-profile.md) | 나만의 카드를 꾸며요. 프로필 정보를 자유롭게 편집할 수 있어요. |
| [계정 연동](docs/account-linking.md) | HYPERGRYPH 게임 계정을 연동해 프로필 정보와 이성 회복까지 남은 시간을 확인해요. |
| [배터리, 저장 공간과 활성 상태 보기](docs/storage-and-activity.md) | 충전 상태, 디스크 공간, CPU, RAM, 실행 중인 앱을 확인해요. |
| [앱 바로가기](docs/app-shortcuts.md) | 원하는 이름과 아이콘으로 앱을 추가해요. |

계정 연동은 선택 사항이에요.

[설정](docs/settings.md)에서 테마, 기울기, 시계, 아이콘, 애니메이션을 바꿀 수 있어요. HUD는 영어, 중국어 간체, 중국어 번체, 일본어, 한국어를 지원해요.

메모와 설정은 Mac에 저장돼요. 연동한 커뮤니티 계정의 인증 정보는 macOS 키체인에 보관해요. 업데이트는 GitHub에서 받아요. 메뉴 막대나 **정보**에서 업데이트를 관리할 수 있어요.

![합체! 오르비폼! 플레이](docs/media/readme/minigame.gif)

<img src="docs/media/readme/en-04-credits.png" alt="크레딧" width="326">

[DDDuoDuo](https://github.com/DDDuoDuo)가 만들었어요. *명일방주: 엔드필드*, [QinAnze](https://github.com/QinAnze/zmd-charge), [llynxxx](https://www.bilibili.com/video/BV1DBaP6yEHs/)에서 영감을 받았어요.

비공식 팬 프로젝트예요. 게임 아트워크와 브랜드의 권리는 HYPERGRYPH와 각 권리자에게 있어요. 직접 작성한 코드는 [MIT 라이선스](LICENSE)를 따르며, 타사 에셋과 라이브러리에는 각각의 이용 조건이 적용돼요. [전체 크레딧과 출처](CREDITS.md)

[버그 제보](https://github.com/DDDuoDuo/EndfieldHUD/issues)
