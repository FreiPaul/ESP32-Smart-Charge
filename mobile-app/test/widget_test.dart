import 'package:flutter_test/flutter_test.dart';

import 'package:esp_led_controller/main.dart';

void main() {
  testWidgets('App loads pairing screen', (WidgetTester tester) async {
    await tester.pumpWidget(const MyApp());
    expect(find.text('Find ESP Device'), findsOneWidget);
  });
}
