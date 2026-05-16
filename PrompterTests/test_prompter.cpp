#include <QtTest>

class Test_Prompter_Placeholder : public QObject
{
    Q_OBJECT

private slots:
    void test_placeholder();
};

void Test_Prompter_Placeholder::test_placeholder()
{
    QVERIFY(true);
}

QTEST_MAIN(Test_Prompter_Placeholder)
#include "test_prompter.moc"
