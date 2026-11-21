/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "xlsx_writer.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <array>
#include <cmath>

namespace
{
constexpr const char* XML_HEADER = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
constexpr const char* MAIN_NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr const char* REL_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

QByteArray columnName(int index)
{
  QByteArray name;
  for (int n = index + 1; n > 0; n = (n - 1) / 26)
  {
    name.prepend(char('A' + (n - 1) % 26));
  }
  return name;
}

QByteArray escapeXml(const QString& text)
{
  QString out;
  out.reserve(text.size());
  for (const QChar c : text)
  {
    switch (c.unicode())
    {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        // XML 1.0 forbids most control characters.
        if (c.unicode() >= 0x20 || c == '\t' || c == '\n' || c == '\r')
        {
          out += c;
        }
    }
  }
  return out.toUtf8();
}

QString sanitizeSheetName(QString name, const QSet<QString>& used)
{
  static const QRegularExpression bad_chars(R"([\[\]:*?/\\])");
  name.replace(bad_chars, "_");
  name = name.trimmed();
  while (name.startsWith('\''))
  {
    name.remove(0, 1);
  }
  while (name.endsWith('\''))
  {
    name.chop(1);
  }
  if (name.isEmpty())
  {
    name = "Sheet";
  }
  name = name.left(31);

  QString candidate = name;
  for (int suffix = 2; used.contains(candidate.toLower()); suffix++)
  {
    const QString tail = QString(" (%1)").arg(suffix);
    candidate = name.left(31 - tail.size()) + tail;
  }
  return candidate;
}

quint32 crc32(const QByteArray& data)
{
  static const auto table = [] {
    std::array<quint32, 256> t{};
    for (quint32 i = 0; i < 256; i++)
    {
      quint32 c = i;
      for (int k = 0; k < 8; k++)
      {
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      t[i] = c;
    }
    return t;
  }();

  quint32 crc = 0xFFFFFFFFu;
  for (const char byte : data)
  {
    crc = table[(crc ^ static_cast<quint8>(byte)) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// Raw deflate stream, as zip expects. qCompress() emits a 4-byte length prefix,
// a 2-byte zlib header and a 4-byte Adler-32 trailer around it.
QByteArray rawDeflate(const QByteArray& data)
{
  const QByteArray zlib = qCompress(data, 6);
  return zlib.mid(6, zlib.size() - 10);
}

class ZipBuilder
{
public:
  ZipBuilder()
  {
    const QDateTime now = QDateTime::currentDateTime();
    const QDate date = now.date();
    const QTime time = now.time();
    _dos_date = static_cast<quint16>(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
    _dos_time =
        static_cast<quint16>((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
  }

  void addFile(const QByteArray& name, const QByteArray& content)
  {
    const QByteArray compressed = rawDeflate(content);
    const quint32 crc = crc32(content);
    const quint32 offset = static_cast<quint32>(_data.size());

    append32(_data, 0x04034b50);
    append16(_data, 20);  // version needed
    append16(_data, 0);   // flags
    append16(_data, 8);   // deflate
    append16(_data, _dos_time);
    append16(_data, _dos_date);
    append32(_data, crc);
    append32(_data, static_cast<quint32>(compressed.size()));
    append32(_data, static_cast<quint32>(content.size()));
    append16(_data, static_cast<quint16>(name.size()));
    append16(_data, 0);  // extra length
    _data += name;
    _data += compressed;

    append32(_central, 0x02014b50);
    append16(_central, 20);  // version made by
    append16(_central, 20);  // version needed
    append16(_central, 0);
    append16(_central, 8);
    append16(_central, _dos_time);
    append16(_central, _dos_date);
    append32(_central, crc);
    append32(_central, static_cast<quint32>(compressed.size()));
    append32(_central, static_cast<quint32>(content.size()));
    append16(_central, static_cast<quint16>(name.size()));
    append16(_central, 0);  // extra length
    append16(_central, 0);  // comment length
    append16(_central, 0);  // disk number
    append16(_central, 0);  // internal attributes
    append32(_central, 0);  // external attributes
    append32(_central, offset);
    _central += name;
    _entries++;
  }

  QByteArray finish() const
  {
    QByteArray out = _data;
    const quint32 central_offset = static_cast<quint32>(out.size());
    out += _central;
    append32(out, 0x06054b50);
    append16(out, 0);
    append16(out, 0);
    append16(out, _entries);
    append16(out, _entries);
    append32(out, static_cast<quint32>(_central.size()));
    append32(out, central_offset);
    append16(out, 0);  // comment length
    return out;
  }

private:
  QByteArray _data;
  QByteArray _central;
  quint16 _entries = 0;
  quint16 _dos_date = 0;
  quint16 _dos_time = 0;

  static void append16(QByteArray& out, quint16 value)
  {
    const quint16 le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char*>(&le), 2);
  }
  static void append32(QByteArray& out, quint32 value)
  {
    const quint32 le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char*>(&le), 4);
  }
};
}  // namespace

namespace PJ
{

int XlsxWriter::addSheet(const QString& name, const QStringList& header)
{
  QSet<QString> used;
  for (const auto& sheet : _sheets)
  {
    used.insert(sheet.name.toLower());
  }

  Sheet sheet;
  sheet.name = sanitizeSheetName(name, used);
  sheet.columns = header.size();
  appendStringRow(sheet, header, true);
  _sheets.push_back(std::move(sheet));
  return static_cast<int>(_sheets.size()) - 1;
}

void XlsxWriter::appendRow(int sheet, const QStringList& cells)
{
  appendStringRow(_sheets.at(sheet), cells, false);
}

void XlsxWriter::appendRow(int sheet_index, const std::vector<double>& cells)
{
  Sheet& sheet = _sheets.at(sheet_index);
  const QByteArray row = QByteArray::number(++sheet.rows);
  sheet.data += "<row r=\"" + row + "\">";
  for (size_t col = 0; col < cells.size(); col++)
  {
    if (!std::isfinite(cells[col]))
    {
      continue;
    }
    sheet.data += "<c r=\"" + columnName(static_cast<int>(col)) + row + "\"><v>";
    sheet.data += QByteArray::number(cells[col], 'g', 15);
    sheet.data += "</v></c>";
  }
  sheet.data += "</row>";
}

void XlsxWriter::appendStringRow(Sheet& sheet, const QStringList& cells, bool bold)
{
  const QByteArray row = QByteArray::number(++sheet.rows);
  sheet.data += "<row r=\"" + row + "\">";
  for (int col = 0; col < cells.size(); col++)
  {
    sheet.data += "<c r=\"" + columnName(col) + row + "\" t=\"inlineStr\"";
    if (bold)
    {
      sheet.data += " s=\"1\"";
    }
    sheet.data += "><is><t xml:space=\"preserve\">" + escapeXml(cells[col]) + "</t></is></c>";
  }
  sheet.data += "</row>";
}

QByteArray XlsxWriter::toByteArray() const
{
  ZipBuilder zip;

  QByteArray content_types = XML_HEADER;
  content_types +=
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" "
      "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
      "<Override PartName=\"/xl/workbook.xml\" "
      "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
      "<Override PartName=\"/xl/styles.xml\" "
      "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
  for (size_t i = 0; i < _sheets.size(); i++)
  {
    content_types += "<Override PartName=\"/xl/worksheets/sheet" + QByteArray::number(int(i + 1)) +
                     ".xml\" ContentType=\"application/"
                     "vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
  }
  content_types += "</Types>";
  zip.addFile("[Content_Types].xml", content_types);

  QByteArray root_rels = XML_HEADER;
  root_rels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
               "<Relationship Id=\"rId1\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
               "officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>";
  zip.addFile("_rels/.rels", root_rels);

  QByteArray workbook = XML_HEADER;
  workbook += QByteArray("<workbook xmlns=\"") + MAIN_NS + "\" xmlns:r=\"" + REL_NS + "\"><sheets>";
  QByteArray workbook_rels = XML_HEADER;
  workbook_rels +=
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
  for (size_t i = 0; i < _sheets.size(); i++)
  {
    const QByteArray id = QByteArray::number(int(i + 1));
    workbook += "<sheet name=\"" + escapeXml(_sheets[i].name) + "\" sheetId=\"" + id +
                "\" r:id=\"rId" + id + "\"/>";
    workbook_rels += "<Relationship Id=\"rId" + id +
                     "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                     "relationships/worksheet\" Target=\"worksheets/sheet" + id + ".xml\"/>";
  }
  workbook += "</sheets></workbook>";
  workbook_rels += "<Relationship Id=\"rId" + QByteArray::number(int(_sheets.size() + 1)) +
                   "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                   "relationships/styles\" Target=\"styles.xml\"/></Relationships>";
  zip.addFile("xl/workbook.xml", workbook);
  zip.addFile("xl/_rels/workbook.xml.rels", workbook_rels);

  QByteArray styles = XML_HEADER;
  styles += QByteArray("<styleSheet xmlns=\"") + MAIN_NS +
            "\">"
            "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
            "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
            "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
            "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
            "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border>"
            "</borders>"
            "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" "
            "borderId=\"0\"/></cellStyleXfs>"
            "<cellXfs count=\"2\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" "
            "xfId=\"0\"/><xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" "
            "applyFont=\"1\"/></cellXfs>"
            "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/>"
            "</cellStyles></styleSheet>";
  zip.addFile("xl/styles.xml", styles);

  for (size_t i = 0; i < _sheets.size(); i++)
  {
    const Sheet& sheet = _sheets[i];
    QByteArray xml = XML_HEADER;
    xml += QByteArray("<worksheet xmlns=\"") + MAIN_NS +
           "\"><sheetViews><sheetView workbookViewId=\"0\">"
           "<pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
           "</sheetView></sheetViews>";
    if (sheet.columns > 0)
    {
      xml += "<cols><col min=\"1\" max=\"" + QByteArray::number(sheet.columns) +
             "\" width=\"22\" customWidth=\"1\"/></cols>";
    }
    xml += "<sheetData>" + sheet.data + "</sheetData></worksheet>";
    zip.addFile("xl/worksheets/sheet" + QByteArray::number(int(i + 1)) + ".xml", xml);
  }

  return zip.finish();
}

}  // namespace PJ
